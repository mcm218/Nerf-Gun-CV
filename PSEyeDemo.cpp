// OpenCVCam.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <winsock2.h>
#include "PSEyeDemo.h"
#include "xPCUDPSock.h"

#pragma pack(push,1)

using namespace std; //allows aceess to all std lib functions without using the namespace std::
using namespace cv; // allows ... without using namespace cv::


#define FRAME_RATE 60
#define RESOLUTION CLEYE_VGA
// QVGA or VGA

struct PACKOUT {
	int panAngle;
	int tiltAngle;
	int fireMode;
	int laserPointer;
};

typedef struct{
	CLEyeCameraInstance CameraInstance;
	Mat *Frame;
	unsigned char *FramePtr;
	int Threshold;
}CAMERA_AND_FRAME;

static DWORD WINAPI CaptureThread(LPVOID ThreadPointer);

Point prevCenter = Point(0,0);
clock_t prevTime;

int _tmain(int argc, _TCHAR* argv[])
{
	int Width,Height;
	int KeyPress;
	CLEyeCameraInstance EyeCamera=NULL;

	Mat Frame;

	CAMERA_AND_FRAME ThreadPointer;
	HANDLE _hThread;
	CLEyeCameraParameter CamCurrentParam=(CLEyeCameraParameter)0;
	bool CamParam=0;


	//////////////////////

	//////////////////// EYE CAMERA SETUP///////////////////////////////////
	// all of this code and more is included in my header file CameraControl.h hence why its commented out
	// I left it here simply for your reference
	EyeCamera=StartCam(FRAME_RATE,RESOLUTION);//this does all the commented out code

	// Get camera frame dimensions;
	CLEyeCameraGetFrameDimensions(EyeCamera, Width, Height);
	// Create a window in which the captured images will be presented
	namedWindow( "Camera", CV_WINDOW_AUTOSIZE );
	//Make a image to hold the frames captured from the camera
	Frame=Mat(Height,Width,CV_8UC4);//8 bit unsiged 4 channel image for Blue Green Red Alpa (8 bit elements per channel)
	//Start the eye camera
	CLEyeCameraStart(EyeCamera);

	/////////////////////////////////////MAIN CODE//////////////////////////////////////

	// For high frame rate launch a seperate thread

	//Need to copy vars into one var to launch the second thread
	ThreadPointer.CameraInstance=EyeCamera;
	ThreadPointer.Frame = &Frame;
	ThreadPointer.Threshold=0;
	//Launch thread and confirm its running
	_hThread = CreateThread(NULL, 0, &CaptureThread, &ThreadPointer, 0, 0);
	if(_hThread == NULL)
	{
		printf("Failed to create thread...");
		getchar();
		return false;
	}
	
		while (1) {

			//This will capture keypresses and do whatever you want if you assign the appropriate actions to the right key code
			KeyPress = waitKey(1);
			switch (KeyPress) {
			case 27: //escape pressed
				return 0;
				break;
			default: //do nothing
				break;
			}

			//Display the captured frame
			imshow("Camera", Frame);
		}
	CLEyeCameraStop(EyeCamera);
	CLEyeDestroyCamera(EyeCamera);
	EyeCamera = NULL;

	return 0;
}

///////////////////////SUB THREAD///////////////////////////
//for high frame rates you will process images here the main function will allow interactions and display only
static DWORD WINAPI CaptureThread(LPVOID ThreadPointer){
	CAMERA_AND_FRAME *Instance=(CAMERA_AND_FRAME*)ThreadPointer; //type cast the void pointer back to the proper type so we can access its elements

	Mat gray, canny, cc_img;
	Mat hsv, mask;

	vector<vector<Point>> contours;
	vector<Vec4i> hierarchy;
	Scalar(ext_color);
	int erosion_size = 3;
	Mat element = getStructuringElement(MORPH_ELLIPSE,
		Size(2 * erosion_size + 1, 2 * erosion_size + 1),
		Point(erosion_size, erosion_size));

	Point input;
	Point massCenter;

	int FramerCounter=0;
	Mat CamImg=Mat(*(Instance->Frame));
	cc_img = Mat(CamImg.cols, CamImg.rows, CV_8UC4);
	
	clock_t StartTime,EndTime;
	Vec4b center;
	Scalar curColor = Scalar(0, 0, 0);
	if (!InitUDPLib()) {
		cout << "UDP Error" << endl;
	}
	else {
		CUDPSender sender(sizeof(PACKOUT), 12302, "127.0.0.1");
		PACKOUT packet;
		packet.panAngle = 1;
		packet.tiltAngle = 1;
		packet.fireMode = 1;
		packet.laserPointer = 1;
		while (1) {
			//Get Frame From Camera
			CLEyeCameraGetFrame(Instance->CameraInstance, CamImg.data);
			//cc_img.setTo(Scalar(0, 0, 0));
			cc_img = CamImg;
			cvtColor(CamImg, gray, CV_BGR2GRAY);
			cvtColor(CamImg, hsv, CV_BGR2HSV);
			inRange(hsv, Scalar(30, 20, 20), Scalar(80, 255, 255), mask);
			//erode and dilate thesholded image to clean up noise 
			//erode(gray, gray, element);
			//dilate(gray, gray, element);
			//canny edge detection
			Canny(gray, canny, 80, 200);

			//create outlines of detected objects
			findContours(canny, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));
			vector<Moments> m(contours.size());
			int found = 0;
			int numContours = 0;
			for (int i = 0; i < contours.size(); i++)
			{
				double area = contourArea(contours[i]);
				//Ignore contour if it is too small
				//200 && 1400 if red cam
				//200 && 700 if blue cam
				if (area > 200 && area < 1400)
				{
					m[i] = moments(contours[i], false);
					massCenter = Point(m[i].m10 / m[i].m00, m[i].m01 / m[i].m00);
					//Threshold the HSV image to get only blue colors
					center = CamImg.at<Vec4b>((int)massCenter.y, (int)massCenter.x);


					//grab area around center pixel and use the average
					//conver to HSV to to check if its green

					//bool isGreen = center[1] > center[2] && center[1] > center[0];
					bool isGreen = true;
					if (isGreen) {
						found++;
						numContours++;
						//draw outline of contours
						drawContours(cc_img, contours, i, curColor, 1);
						//Draw center of mass
						circle(cc_img, massCenter, 2, curColor, 1, 8, 0);
						//predict future location
						if (prevCenter.x == 0 && prevCenter.y == 0) {
							prevCenter = massCenter;
							prevTime = clock();
						}
						double timeDiff = (clock() - prevTime) / CLOCKS_PER_SEC;
						double xDiff = massCenter.x - prevCenter.x;
						double yDiff = massCenter.y - prevCenter.y;
						//find x & y velocity in pixels/second
						double xVel = xDiff / timeDiff;
						double yVel = xDiff / timeDiff;
						//draw predicted location 1 sec from present
						Point predPos = Point(massCenter.x + xVel, massCenter.y + yVel);
						circle(cc_img, predPos, 2, Scalar(255, 255, 0), 1, 8, 0);
						//update prev values
						prevCenter = massCenter;
						prevTime = clock();
						//set packet values
						/*packet.panAngle = (int) massCenter.x;
						packet.tiltAngle = (int) massCenter.y;
						packet.fireMode = 1;
						packet.laserPointer = 1;*/
						packet.panAngle++;
						packet.tiltAngle++;
						packet.fireMode++;
						packet.laserPointer++;

						sender.SendData(&packet);
						//Sleep(1);
					}
				}
			}
			if (found == 0) {
				//cout << "Found Nothing" << endl;
			}
			if (found > 1) {
				//cout << "Found too many" << endl;
			}

			//print values out to file to check that its stable
			//copy it to main thread image.
			*(Instance->Frame) = cc_img;
			// Track FPS
			if (FramerCounter == 0) StartTime = clock();

			FramerCounter++;
			EndTime = clock();
			if ((EndTime - StartTime) / CLOCKS_PER_SEC >= 1) {
				cout << "Contours: " << numContours << endl;
				cout << "FPS:" << FramerCounter << endl;
				FramerCounter = 0;
			}
		}
	}
	return 0;
}

