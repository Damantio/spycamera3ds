//#include
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <3ds.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <stdbool.h>
#include <3ds/svc.h>
#include "mem.h"

//#define
#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define WAIT_TIMEOUT 300000000ULL
#define WIDTH 400
#define HEIGHT 240
#define SCREEN_SIZE WIDTH * HEIGHT * 2
#define BUF_SIZE SCREEN_SIZE * 2

static jmp_buf exitJmp;



inline void clearScreen(void) {
	u8 *frame = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(frame, 0, 320 * 240 * 3);
}

void hang(char *message) {
	clearScreen();
	printf("%s", message);
	printf("Press start to exit");

	while (aptMainLoop()) {
		hidScanInput();

		u32 kHeld = hidKeysHeld();
		if (kHeld & KEY_START) longjmp(exitJmp, 1);
	}
}

//final closing
void cleanup() {
	camExit();
	gfxExit();
	acExit();
}

//draw to top screen
void writePictureToFramebufferRGB565(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
	u8 *fb_8 = (u8*) fb;
	u16 *img_16 = (u16*) img;
	int i, j, draw_x, draw_y;
	for(j = 0; j < height; j++) {
		for(i = 0; i < width; i++) {
			draw_y = y + height - j;
			draw_x = x + i;
			u32 v = (draw_y + draw_x * height) * 3;
			u16 data = img_16[j * width + i];
			uint8_t b = ((data >> 11) & 0x1F) << 3;
			uint8_t g = ((data >> 5) & 0x3F) << 2;
			uint8_t r = (data & 0x1F) << 3;
			fb_8[v] = r;
			fb_8[v+1] = g;
			fb_8[v+2] = b;
		}
	}
}

// take picture (Semi-3D) --- use CAMU_GetStereoCameraCalibrationData
void takePicture3D(u8 *buf) {
	u32 bufSize;
	printf("CAMU_GetMaxBytes: 0x%08X\n", (unsigned int) CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
	printf("CAMU_SetTransferBytes: 0x%08X\n", (unsigned int) CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT));

	printf("CAMU_Activate: 0x%08X\n", (unsigned int) CAMU_Activate(SELECT_OUT1_OUT2));

	Handle camReceiveEvent = 0;
	Handle camReceiveEvent2 = 0;

	printf("CAMU_ClearBuffer: 0x%08X\n", (unsigned int) CAMU_ClearBuffer(PORT_BOTH));
	printf("CAMU_SynchronizeVsyncTiming: 0x%08X\n", (unsigned int) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2));

	printf("CAMU_StartCapture: 0x%08X\n", (unsigned int) CAMU_StartCapture(PORT_BOTH));

	printf("CAMU_SetReceiving: 0x%08X\n", (unsigned int) CAMU_SetReceiving(&camReceiveEvent, buf, PORT_CAM1, SCREEN_SIZE, (s16) bufSize));
	printf("CAMU_SetReceiving: 0x%08X\n", (unsigned int) CAMU_SetReceiving(&camReceiveEvent2, buf + SCREEN_SIZE, PORT_CAM2, SCREEN_SIZE, (s16) bufSize));
	printf("svcWaitSynchronization: 0x%08X\n", (unsigned int) svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT));
	printf("svcWaitSynchronization: 0x%08X\n", (unsigned int) svcWaitSynchronization(camReceiveEvent2, WAIT_TIMEOUT));
	//printf("CAMU_PlayShutterSound: 0x%08X\n", (unsigned int) CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL));

	printf("CAMU_StopCapture: 0x%08X\n", (unsigned int) CAMU_StopCapture(PORT_BOTH));

	svcCloseHandle(camReceiveEvent);
	svcCloseHandle(camReceiveEvent2);

	printf("CAMU_Activate: 0x%08X\n", (unsigned int) CAMU_Activate(SELECT_NONE));
}


//code from 3DS Paint, used to screenshot
bool SaveDrawing(char* path)
{
	int x, y;

	FS_archive sdmcArchive;
	sdmcArchive = (FS_archive){ 0x9, (FS_path){ PATH_EMPTY, 1, (u8*)"" } };
	FSUSER_OpenArchive(NULL, &sdmcArchive);

	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(path) + 1;
	filePath.data = (u8*)path;

	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_CREATE | FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if (res)
		return false;

	u32 byteswritten;

	u32 bitmapsize = 320 * 205 * 3;
	u8* tempbuf = (u8*)MemAlloc(0x36 + bitmapsize);
	memset(tempbuf, 0, 0x36 + bitmapsize);

	FSFILE_SetSize(file, (u16)(0x36 + bitmapsize));

	*(u16*)&tempbuf[0x0] = 0x4D42;
	*(u32*)&tempbuf[0x2] = 0x36 + bitmapsize;
	*(u32*)&tempbuf[0xA] = 0x36;
	*(u32*)&tempbuf[0xE] = 0x28;
	*(u32*)&tempbuf[0x12] = 320; // width
	*(u32*)&tempbuf[0x16] = 205; // height
	*(u32*)&tempbuf[0x1A] = 0x00180001;
	*(u32*)&tempbuf[0x22] = bitmapsize;

	u8* framebuf = (u8*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	for (y = 35; y < 240; y++)
	{
		for (x = 0; x < 320; x++)
		{
			int si = ((239 - y) + (x * 240)) * 3;
			int di = 0x36 + (x + ((239 - y) * 320)) * 3;

			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
			tempbuf[di++] = framebuf[si++];
		}
	}

	FSFILE_Write(file, &byteswritten, 0, (u32*)tempbuf, 0x36 + bitmapsize, 0x10001);

	FSFILE_Close(file);
	MemFree(tempbuf);
	return true;
 }

//code to sun 3DS paint's code
void screenShot() {
	//time is used as lazy sorting - also taken from 3DS Paint
	u32 timestamp = (u32)(svcGetSystemTick() / 446872);
	char file[256];
	snprintf(file, 256, "/cam/pic%08d.bmp", timestamp);
	if (SaveDrawing(file)){

	}
}

//Biggest & Baddest doer of things
int main() {
	// Initializations
	acInit();
	gfxInitDefault();
	csndInit();
	consoleInit(GFX_BOTTOM, NULL);

	//loading of ton.wav into ram
	FILE *file = fopen("tone.wav", "rb");
	fseek(file, 0, SEEK_END);
	u32 sndSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	sndSize = sndSize - 0x48;
	fseek(file, 0x48, SEEK_SET);
	u8 *sndBuf = linearAlloc(sndSize);
	fread(sndBuf, 1, sndSize, file);
	fclose(file);





	// Enable double buffering to remove screen tearing
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	// Save current stack frame for easy exit
	if(setjmp(exitJmp)) {
		cleanup();
		return 0;
	}

	u32 kDown;
	u32 kHeld;

	//Debug & setup
	printf("Initializing camera\n");
	printf("camInit: 0x%08X\n", (unsigned int) camInit());
	printf("CAMU_SetSize: 0x%08X\n", (unsigned int) CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_CTR_TOP_LCD, CONTEXT_A));
	printf("CAMU_SetOutputFormat: 0x%08X\n", (unsigned int) CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A));
	printf("CAMU_SetNoiseFilter: 0x%08X\n", (unsigned int) CAMU_SetNoiseFilter(SELECT_OUT1_OUT2, true));
	printf("CAMU_SetAutoExposure: 0x%08X\n", (unsigned int) CAMU_SetAutoExposure(SELECT_OUT1_OUT2, true));
	printf("CAMU_SetAutoWhiteBalance: 0x%08X\n", (unsigned int) CAMU_SetAutoWhiteBalance(SELECT_OUT1_OUT2, true));
	//printf("CAMU_SetEffect: 0x%08X\n", (unsigned int) CAMU_SetEffect(SELECT_OUT1_OUT2, EFFECT_MONO, CONTEXT_A));
	printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int) CAMU_SetTrimming(PORT_CAM1, false));
	printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int) CAMU_SetTrimming(PORT_CAM2, false));
	//printf("CAMU_SetTrimmingParamsCenter: 0x%08X\n", (unsigned int) CAMU_SetTrimmingParamsCenter(PORT_CAM1, 512, 240, 512, 384));

	//small safety measure
	u8 *buf = malloc(BUF_SIZE);
	if(!buf) {
		hang("Failed to allocate memory!");
	}

	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxSwapBuffers();

	bool held_R = false;
	int i = 0;
	int waitTime = 500;
	int count = 0;
	int maxCount = 20;
	bool setup = false;

	consoleClear();
	//informa l'utente
	printf("\nMaxCount (SX/DX)\n");
	printf("\nWaitTime (SU/GIU')\n");
	printf("\nPremi 'A' per cominciare\n");
	while(!setup){

		hidScanInput();
		kDown = hidKeysDown();
		kHeld = hidKeysHeld();


		if (kDown & KEY_A){
			setup = !setup;
		}

		if (kDown){
			if (kDown & KEY_DUP){
				waitTime = waitTime + 1;
			}
			if (kDown & KEY_DDOWN){
				waitTime = waitTime - 1;
			}
			if (kDown & KEY_DLEFT){
				maxCount = maxCount - 1;
			}
			if (kDown & KEY_DRIGHT){
				maxCount = maxCount + 1;
			}
			char w[5];
			char m[5];
			sprintf(w,"%d",waitTime);
			sprintf(m,"%d",maxCount);

			consoleClear();
			printf("\nMaxCount (Left/Right)\n");
			printf(m);
			printf("\nWaitTime (Up/Down)\n");
			printf(w);
			printf("\nPremi 'A' per cominciare\n");
			printf("Attendere..."); //Di solito non viene mostrato

		}
	}

	//Per gli utenti smemorati
	printf("\nPremi start per uscire dall'applicazione\n");
	printf("Scatto automatico in funzione...\n");

	// Main loop
	while (aptMainLoop()) {
		// Read which buttons are currently pressed or not
		hidScanInput();
		kDown = hidKeysDown();
		kHeld = hidKeysHeld();

		// If START button is pressed, break loop and quit
		if (kDown & KEY_START) {
			break;
		}

		//Logic is Weird...
		if ((kHeld & KEY_R) && !held_R) {
			held_R = true;
		}
		if (!(kHeld & KEY_R)) {
			held_R = false;
		}

		//photo takin code
		if (held_R || (i == waitTime && count < maxCount)){
			printf("Capturing new image\n");
			gfxFlushBuffers();
			gspWaitForVBlank();
			gfxSwapBuffers();

			takePicture3D(buf);
			screenShot();

			//Beep(); if only...
			csndPlaySound(0x8, SOUND_FORMAT_16BIT, 44100, 1, 0, sndBuf, sndBuf, sndSize);
			i = 0;
			count = count + 1;
		} else {
			i = i + 1;
		}

		//3D needs to be implemented...
		if(CONFIG_3D_SLIDERSTATE > 0.0f) {
			gfxSet3D(true);
			writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), buf, 0, 0, WIDTH, HEIGHT);
			writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), buf + SCREEN_SIZE, 0, 0, WIDTH, HEIGHT);
		} else {
			gfxSet3D(false);
			writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), buf, 0, 0, WIDTH, HEIGHT);
		}

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gspWaitForVBlank();
		gfxSwapBuffers();

		//sleep(1000);
	}

	printf("\nChiusura dell'applicazione");

	// Exit
	free(buf);
	csndExecCmds(true);
	CSND_SetPlayState(0x8, 0);
	csndExecCmds(true);
	memset(sndBuf, 0, sndSize);
	GSPGPU_FlushDataCache(NULL, sndBuf, sndSize);
	csndExit();
	linearFree(sndBuf);
	cleanup();

	// Return to hbmenu
	return 0;
}
