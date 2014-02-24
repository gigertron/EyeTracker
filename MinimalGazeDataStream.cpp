/*
 * This is an example that demonstrates how to connect to the EyeX Engine and subscribe to the lightly filtered gaze data stream.
 *
 * Copyright 2013 Tobii Technology AB. All rights reserved.
 */


#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <assert.h>
#include <commdlg.h>
#include <basetsd.h>
#include <objbase.h>

#include "eyex\EyeX.h"
//XIpunt for joystick support

#ifdef USE_DIRECTX_SDK
#include <C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\include\xinput.h>
#pragma comment(lib,"xinput.lib")
#elif (_WIN32_WINNT >= 0x0604 ) //_WIN32_WINNT_WIN8
#include <XInput.h>
#pragma comment(lib,"xinput.lib")
#else
#include <XInput.h>
#pragma comment(lib,"xinput9_1_0.lib")
#endif

#pragma comment (lib, "Tobii.EyeX.Client.lib")

void HandleNewCoords(float x, float y);

HRESULT UpdateControllerState();
#define MAX_CONTROLLERS 4  // XInput handles up to 4 controllers 
#define INPUT_DEADZONE 8000// ( 0.24f * FLOAT(0x7FFF) )  // Default to 24% of the +/- 32767 range.   This is a reasonable default value but can be altered if needed.

struct CONTROLLER_STATE
{
	XINPUT_STATE state;
	BOOL bConnected;
};

CONTROLLER_STATE g_Controllers[MAX_CONTROLLERS];
WCHAR g_szMessage[4][1024] = { 0 };
bool g_joystickOverride = false;
float leftJoySpeed = 0.0005f;
float rightJoySpeed = 0.0001f;
BYTE g_joyTriggerR = 0;
BYTE g_joyTriggerL = 0;

DWORD g_joystickTimeout = 500;//milliseconds aka ticks
DWORD g_lastJoyTime = 0;

// ID of the global interactor that provides our data stream; must be unique within the application.
static const TX_STRING InteractorIdGaze = "Twilight Sparkle";
static const TX_STRING InteractorIdFix = "Rainbow Dash";

// global variables
static TX_HANDLE g_hGlobalGazeInteractorSnapshot = TX_EMPTY_HANDLE;
static TX_HANDLE g_hGlobalFixInteractorSnapshot = TX_EMPTY_HANDLE;

float lastRawX = 0;
float lastRawY = 0;

float filteredX = 0;
float filteredY = 0;
float integratedErrorX = 0;
float integratedErrorY = 0;
float integratingSpeed = 0.001f;
const float FIntegrationDeadZone = 500.0; //pixels
const float FDeadZone = 50.0;  // pixels
const float g_SlowZone = 1; // pixels
const float FSpeed = 0.8f; // how fast to move when far away
const float FSlowSpeed = 0.1f; // now fast to move when close
const float FVerySlowSpeed = 0.05f;
float filteredErrorX = 0; // this will be the distance between the gaze data and the current mouse position
float filteredErrorY = 0; // this will be the distance between the gaze data and the current mouse position



/*
 * Initializes g_hGlobalInteractorSnapshot with an interactor that has the Gaze Point behavior.
 */
BOOL InitializeGlobalInteractorSnapshot(TX_HANDLE hContext)
{
	TX_HANDLE hInteractor = TX_EMPTY_HANDLE;
	TX_HANDLE hBehavior = TX_EMPTY_HANDLE;
	TX_HANDLE hFixInteractor = TX_EMPTY_HANDLE;
	TX_HANDLE hFixBehavior = TX_EMPTY_HANDLE;
//	TX_GAZEPOINTDATAPARAMS params = { TX_GAZEPOINTDATAMODE_LIGHTLYFILTERED };
	TX_GAZEPOINTDATAPARAMS params = { TX_GAZEPOINTDATAMODE_LIGHTLYFILTERED };
	TX_FIXATIONDATAPARAMS fixParams = { TX_FIXATIONDATAMODE_SENSITIVE };
	BOOL success;

	success = txCreateGlobalInteractorSnapshot(
		hContext,
		InteractorIdGaze,
		&g_hGlobalGazeInteractorSnapshot,
		&hInteractor) == TX_RESULT_OK;
	success &= txCreateInteractorBehavior(hInteractor, &hBehavior, TX_INTERACTIONBEHAVIORTYPE_GAZEPOINTDATA) == TX_RESULT_OK;
	success &= txSetGazePointDataBehaviorParams(hBehavior, &params) == TX_RESULT_OK;
	


	success = txCreateGlobalInteractorSnapshot(
		hContext,
		InteractorIdFix,
		&g_hGlobalFixInteractorSnapshot,
		&hFixInteractor) == TX_RESULT_OK;
	success &= txCreateInteractorBehavior(hFixInteractor, &hFixBehavior, TX_INTERACTIONBEHAVIORTYPE_FIXATIONDATA) == TX_RESULT_OK;
	success &= txSetFixationDataBehaviorParams(hFixBehavior, &fixParams) == TX_RESULT_OK;

	txReleaseObject(&hBehavior);
	txReleaseObject(&hInteractor);
	txReleaseObject(&hFixBehavior);
	txReleaseObject(&hFixInteractor);

	return success;
}

/*
 * Callback function invoked when a snapshot has been committed.
 */
void TX_CALLCONVENTION OnSnapshotCommitted(TX_HANDLE hSnapshot, TX_HANDLE hResult, TX_USERPARAM param)
{
	// check the result code using an assertion.
	// this will catch validation errors and runtime errors in debug builds. in release builds it won't do anything.

	TX_SNAPSHOTRESULTCODE resultCode = TX_SNAPSHOTRESULTCODE_UNKNOWNERROR;
	txGetSnapshotResultCode(hResult, &resultCode);
	assert(resultCode == TX_SNAPSHOTRESULTCODE_OK || resultCode == TX_COMMANDRESULTCODE_CANCELLED);

	txReleaseObject(&hSnapshot);
	txReleaseObject(&hResult);
}

/*
 * Callback function invoked when the status of the connection to the EyeX Engine has changed.
 */
void TX_CALLCONVENTION OnEngineConnectionStateChanged(TX_CONNECTIONSTATE connectionState, TX_USERPARAM userParam)
{
	switch (connectionState) {
	case TX_CONNECTIONSTATE_CONNECTED: {
			BOOL success;
			printf("The connection state is now CONNECTED.\n");
			// commit the snapshot with the global interactor as soon as the connection to the engine is established.
			// (it cannot be done earlier because committing means "send to the engine".)
			success = txCommitSnapshot(g_hGlobalGazeInteractorSnapshot, OnSnapshotCommitted, NULL) == TX_RESULT_OK;
			success &= txCommitSnapshot(g_hGlobalFixInteractorSnapshot, OnSnapshotCommitted, NULL) == TX_RESULT_OK;
			if (!success) {
				printf("Failed to initialize the data stream.\n");
			}
		}
		break;

	case TX_CONNECTIONSTATE_DISCONNECTED:
		printf("The connection state is now DISCONNECTED.\n");
		break;

	case TX_CONNECTIONSTATE_TRYINGTOCONNECT:
		printf("The connection state is now TRYINGTOCONNECT.\n");
		break;

	case TX_CONNECTIONSTATE_SERVERVERSIONTOOLOW:
		printf("The connection state is now SERVER_VERSION_TOO_LOW: this application requires a more recent version of the EyeX Engine to run.\n");
		break;

	case TX_CONNECTIONSTATE_SERVERVERSIONTOOHIGH:
		printf("The connection state is now SERVER_VERSION_TOO_HIGH: this application requires an older version of the EyeX Engine to run.\n");
		break;
	}
}

/*
* Handles an event from the Gaze Point data stream.
*/
void OnGazeDataEvent(TX_HANDLE hGazeDataBehavior)
{
	TX_GAZEPOINTDATAEVENTPARAMS eventParams;
	if (txGetGazePointDataEventParams(hGazeDataBehavior, &eventParams) == TX_RESULT_OK) {
		printf("Gaze Data: (%.1f, %.1f) timestamp %.0f ms\n", eventParams.X, eventParams.Y, eventParams.Timestamp);
		HandleNewCoords((float)eventParams.X, (float)eventParams.Y);
	}
	else {
		printf("Failed to interpret gaze data event packet.\n");
	}

}
/*
* Handles an event from the Fixation Point data stream.
*/
void OnFixationDataEvent(TX_HANDLE hFixDataBehavior)
{
	TX_FIXATIONDATAEVENTPARAMS eventParams;
	if (txGetFixationDataEventParams(hFixDataBehavior, &eventParams) == TX_RESULT_OK) {
		printf("Fix  Data: (%.1f, %.1f) timestamp %.0f ms\n", eventParams.X, eventParams.Y, eventParams.Timestamp);
		//HandleNewCoords(eventParams.X, eventParams.Y);
	}
	else {
		printf("Failed to interpret fix data event packet.\n");
	}
}

void HandleNewCoords(float x, float y)
{
	if (g_joystickOverride) return; // don't even look at eye gaze data if the joystick's active
	if (GetTickCount() < (g_lastJoyTime + g_joystickTimeout)) return; // delay a bit before taking over again

	if (abs(lastRawX - x) + abs(lastRawY - y) > 50)
	{
		lastRawX = x;
		lastRawY = y;
		return;//reject noise
	}

	float errorX = x - filteredX;
	float errorY = y - filteredY;

	integratedErrorX += errorX;
	integratedErrorY += errorY;

	float speed = FSlowSpeed;
	if (abs(errorX + errorY ) > g_SlowZone) speed = FSpeed;
	if (g_joystickOverride)
	{
		float joySpeed = 1.0 - ((float)(g_joyTriggerR) / 190.0); // fully pulled trigger will be '0', not touched will be '1'
		if (joySpeed < 0)joySpeed = 0;
		speed = FVerySlowSpeed * joySpeed;
	}

	if (abs(errorX) > FDeadZone){ filteredX += speed * errorX; }
	if (abs(errorY) > FDeadZone){ filteredY += speed * errorY; }

	//		if (abs(integratedErrorX) > FIntegrationDeadZone){ filteredX += integratingSpeed * integratedErrorX; integratedErrorX = 0; }
	//		if (abs(integratedErrorY) > FIntegrationDeadZone){ filteredY += integratingSpeed * integratedErrorY; integratedErrorY = 0; }

	SetCursorPos((int)filteredX, (int)filteredY);
}

/*
 * Callback function invoked when an event has been received from the EyeX Engine.
 */
void TX_CALLCONVENTION HandleEvent(TX_HANDLE hObject, TX_USERPARAM userParam)
{
	TX_HANDLE hBehavior = TX_EMPTY_HANDLE;

	// NOTE. Uncomment the following line of code to view the event object. The same function can be used with any interaction object.
	//OutputDebugStringA(txDebugObject(hObject));

	
	if (txGetEventBehavior(hObject, &hBehavior, TX_INTERACTIONBEHAVIORTYPE_GAZEPOINTDATA) == TX_RESULT_OK) {
		OnGazeDataEvent(hBehavior);
		txReleaseObject(&hBehavior);
	}
	
	if (txGetEventBehavior(hObject, &hBehavior, TX_INTERACTIONBEHAVIORTYPE_FIXATIONDATA) == TX_RESULT_OK) {
		OnFixationDataEvent(hBehavior);
		txReleaseObject(&hBehavior);
	}
	// NOTE since this is a very simple application with a single interactor and a single data stream, 
	// our event handling code can be very simple too. A more complex application would typically have to 
	// check for multiple behaviors and route events based on interactor IDs.

	txReleaseObject(&hObject);
}

/*
 * Application entry point.
 */
int _tmain(int argc, _TCHAR* argv[])
{
	TX_HANDLE hContext = TX_EMPTY_HANDLE;
	TX_TICKET hConnectionStateChangedTicket = TX_INVALID_TICKET;
	TX_TICKET hEventHandlerTicket = TX_INVALID_TICKET;
	BOOL success;

	// initialize and enable the context that is our link to the EyeX Engine.
	success = txInitializeSystem(TX_SYSTEMCOMPONENTOVERRIDEFLAG_NONE, NULL, NULL) == TX_RESULT_OK;
	success &= txCreateContext(&hContext, TX_FALSE) == TX_RESULT_OK;
	success &= InitializeGlobalInteractorSnapshot(hContext);
	success &= txRegisterConnectionStateChangedHandler(hContext, &hConnectionStateChangedTicket, OnEngineConnectionStateChanged, NULL) == TX_RESULT_OK;
	success &= txRegisterEventHandler(hContext, &hEventHandlerTicket, HandleEvent, NULL) == TX_RESULT_OK;
	success &= txEnableConnection(hContext) == TX_RESULT_OK;
	

	// let the events flow until a key is pressed.
	if (success) {
		printf("Initialization was successful.\n");
	} else {
		printf("Initialization failed.\n");
	}
	printf("Press any key to exit...\n");

	while (1)
	{
		UpdateControllerState();
		Sleep(16);
	}

	_gettch();
	printf("Exiting.\n");

	// disable and delete the context.
	txDisableConnection(hContext);
	txReleaseObject(&g_hGlobalGazeInteractorSnapshot);
	txReleaseObject(&g_hGlobalFixInteractorSnapshot);
	txReleaseContext(&hContext, TX_CLEANUPTIMEOUT_DEFAULT, TX_TRUE);

	return 0;
}

bool justClicked = false;
bool justRightClicked = false;

//-----------------------------------------------------------------------------
HRESULT UpdateControllerState()
{
	DWORD dwResult;
	for (DWORD i = 0; i < MAX_CONTROLLERS; i++)
	{
		// Simply get the state of the controller from XInput.
		dwResult = XInputGetState(i, &g_Controllers[i].state);

		if (dwResult == ERROR_SUCCESS)
			g_Controllers[i].bConnected = TRUE;
		else
			g_Controllers[i].bConnected = FALSE;
	}


	CHAR sz[4][1024];
	for (DWORD i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (g_Controllers[i].bConnected)
		{
			WORD wButtons = g_Controllers[i].state.Gamepad.wButtons;

			if ((wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER || wButtons & XINPUT_GAMEPAD_A) && !justClicked)
			{
				justClicked = true;
				INPUT input;
				input.type = INPUT_MOUSE;
				input.mi.dx = 0;
				input.mi.dy = 0;
				input.mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN);
				input.mi.mouseData = 0;
				input.mi.dwExtraInfo = NULL;
				input.mi.time = 0;
				SendInput(1, &input, sizeof(INPUT));
			}
			if (!(wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER || wButtons & XINPUT_GAMEPAD_A) && justClicked)
			{
				justClicked = false;
				INPUT input;
				input.type = INPUT_MOUSE;
				input.mi.dx = 0;
				input.mi.dy = 0;
				input.mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP);
				input.mi.mouseData = 0;
				input.mi.dwExtraInfo = NULL;
				input.mi.time = 0;
				SendInput(1, &input, sizeof(INPUT));
			}

			if ((wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER || wButtons & XINPUT_GAMEPAD_B) && !justRightClicked)
			{
				justRightClicked = true;
				INPUT input;
				input.type = INPUT_MOUSE;
				input.mi.dx = 0;
				input.mi.dy = 0;
				input.mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTDOWN);
				input.mi.mouseData = 0;
				input.mi.dwExtraInfo = NULL;
				input.mi.time = 0;
				SendInput(1, &input, sizeof(INPUT));
			}
			if (!(wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER || wButtons & XINPUT_GAMEPAD_B) && justRightClicked)
			{
				justRightClicked = false;
				INPUT input;
				input.type = INPUT_MOUSE;
				input.mi.dx = 0;
				input.mi.dy = 0;
				input.mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTUP);
				input.mi.mouseData = 0;
				input.mi.dwExtraInfo = NULL;
				input.mi.time = 0;
				SendInput(1, &input, sizeof(INPUT));
			}

			// Zero value if thumbsticks are within the dead zone 
			if ((g_Controllers[i].state.Gamepad.sThumbLX < INPUT_DEADZONE &&
				g_Controllers[i].state.Gamepad.sThumbLX > -INPUT_DEADZONE) &&
				(g_Controllers[i].state.Gamepad.sThumbLY < INPUT_DEADZONE &&
				g_Controllers[i].state.Gamepad.sThumbLY > -INPUT_DEADZONE))
			{
				g_joystickOverride = false;// allow eye gaze control
				g_Controllers[i].state.Gamepad.sThumbLX = 0;
				g_Controllers[i].state.Gamepad.sThumbLY = 0;
			}
			else
			{
				g_joystickOverride = true; // override eye gaze control - adjust mouse position with joystick
				filteredX += g_Controllers[i].state.Gamepad.sThumbLX * leftJoySpeed;
				filteredY -= g_Controllers[i].state.Gamepad.sThumbLY * leftJoySpeed;
				if (filteredY < 0)filteredY = 0;
				if (filteredX < 0)filteredX = 0;
				if (filteredX > 4096)filteredX = 4096;
				if (filteredY > 4096)filteredY = 4096;

				SetCursorPos((int)filteredX, (int)filteredY);
				g_lastJoyTime = GetTickCount();
			}


			if ((g_Controllers[i].state.Gamepad.sThumbRX < INPUT_DEADZONE &&
				g_Controllers[i].state.Gamepad.sThumbRX > -INPUT_DEADZONE) &&
				(g_Controllers[i].state.Gamepad.sThumbRY < INPUT_DEADZONE &&
				g_Controllers[i].state.Gamepad.sThumbRY > -INPUT_DEADZONE))
			{
				g_joystickOverride = false;// allow eye gaze control
				g_Controllers[i].state.Gamepad.sThumbRX = 0;
				g_Controllers[i].state.Gamepad.sThumbRY = 0;
			}
			else
			{
				g_joystickOverride = true; // override eye gaze control - adjust mouse position with joystick
				filteredX += g_Controllers[i].state.Gamepad.sThumbRX * rightJoySpeed; // right joystick will be slower
				filteredY -= g_Controllers[i].state.Gamepad.sThumbRY * rightJoySpeed;
				if (filteredY < 0)filteredY = 0;
				if (filteredX < 0)filteredX = 0;
				if (filteredX > 4096)filteredX = 4096;
				if (filteredY > 4096)filteredY = 4096;

				SetCursorPos((int)filteredX, (int)filteredY);
				g_lastJoyTime = GetTickCount();
			}

			// if either of the triggers are pulled, suspend eye gaze tracking
			if (g_Controllers[i].state.Gamepad.bLeftTrigger > 10 || g_Controllers[i].state.Gamepad.bRightTrigger > 10)
			{
				g_joyTriggerL = g_Controllers[i].state.Gamepad.bLeftTrigger;
				g_joyTriggerR = g_Controllers[i].state.Gamepad.bRightTrigger;

				g_joystickOverride = true;
				g_lastJoyTime = GetTickCount();
			}

			sprintf_s(sz[i], 1024,
				"Controller %u: Connected\n"
				"  Buttons: %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n"
				"  Left Trigger: %u\n"
				"  Right Trigger: %u\n"
				"  Left Thumbstick: %d/%d\n"
				"  Right Thumbstick: %d/%d", i,
				(wButtons & XINPUT_GAMEPAD_DPAD_UP) ? "DPAD_UP " : "",
				(wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? "DPAD_DOWN " : "",
				(wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? "DPAD_LEFT " : "",
				(wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? "DPAD_RIGHT " : "",
				(wButtons & XINPUT_GAMEPAD_START) ? "START " : "",
				(wButtons & XINPUT_GAMEPAD_BACK) ? "BACK " : "",
				(wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? "LEFT_THUMB " : "",
				(wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? "RIGHT_THUMB " : "",
				(wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? "LEFT_SHOULDER " : "",
				(wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? "RIGHT_SHOULDER " : "",
				(wButtons & XINPUT_GAMEPAD_A) ? "A " : "",
				(wButtons & XINPUT_GAMEPAD_B) ? "B " : "",
				(wButtons & XINPUT_GAMEPAD_X) ? "X " : "",
				(wButtons & XINPUT_GAMEPAD_Y) ? "Y " : "",
				g_Controllers[i].state.Gamepad.bLeftTrigger,
				g_Controllers[i].state.Gamepad.bRightTrigger,
				g_Controllers[i].state.Gamepad.sThumbLX,
				g_Controllers[i].state.Gamepad.sThumbLY,
				g_Controllers[i].state.Gamepad.sThumbRX,
				g_Controllers[i].state.Gamepad.sThumbRY);
		}
		else
		{
			sprintf_s(sz[i],1024, "Controller %u: Not connected", i);
		}
//		printf("%s", sz[i]);
	}


	return S_OK;
}
