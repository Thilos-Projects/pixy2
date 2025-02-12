//
// begin license header
//
// This file is part of Pixy CMUcam5 or "Pixy" for short
//
// All Pixy source code is provided under the terms of the
// GNU General Public License v2 (http://www.gnu.org/licenses/gpl-2.0.html).
// Those wishing to use Pixy source code, software and/or
// technologies under different licensing terms should contact us at
// cmucam@cs.cmu.edu. Such licensing terms are available for
// all portions of the Pixy codebase presented here.
//
// end license header
//

#include <stdio.h>
#include "progvideo.h"
#include "pixy_init.h"
#include "camera.h"
#include "blobs.h"
#include "conncomp.h"
#include "param.h"
#include "led.h"
#include "smlink.hpp"
#include "misc.h"
#include "pixyvals.h"
#include "serial.h"
#include "calc.h"
#include <string.h>

static uint8_t g_rgbSize = VIDEO_RGB_SIZE;

REGISTER_PROG(ProgVideo, PROG_NAME_VIDEO, "continuous stream of raw camera frames", PROG_VIDEO_MIN_TYPE, PROG_VIDEO_MAX_TYPE);
ProgVideo::ProgVideo(uint8_t progIndex)
{	
	if (g_execArg==0)
		cam_setMode(CAM_MODE0);
	else
		cam_setMode(CAM_MODE1);

	// run m0 
	exec_runM0(1);
	SM_OBJECT->currentLine = 0;
	SM_OBJECT->stream = 1;
}

ProgVideo::~ProgVideo()
{
	exec_stopM0();
}

int ProgVideo::loop(char *status)
{
	while(SM_OBJECT->currentLine < CAM_RES2_HEIGHT-2)
	{
		if (SM_OBJECT->stream==0)
			printf("not streaming\n");
	}
	SM_OBJECT->stream = 0; // pause after frame grab is finished
	
	// send over USB 
	if (g_execArg==0)
		cam_sendFrame(g_chirpUsb, CAM_RES2_WIDTH, CAM_RES2_HEIGHT);
	else
		sendCustom();
	// resume streaming
	SM_OBJECT->currentLine = 0;
	SM_OBJECT->stream = 1; // resume streaming

	return 0;
}

uint32_t getRGB(uint16_t x, uint16_t y, uint8_t sat)
{
	uint32_t rgb;
	uint8_t r, g, b;
	uint16_t i, j, rsum, gsum, bsum, d;
	int16_t x0, x1, y0, y1;
	uint8_t *p = (uint8_t *)SRAM1_LOC + CAM_PREBUF_LEN;
	// average a square of size W
	
	if (x>=CAM_RES2_WIDTH)
		x = CAM_RES2_WIDTH-1;
	if (y>=CAM_RES2_HEIGHT)
		y = CAM_RES2_HEIGHT-1;
	
	x0 = x-g_rgbSize;
	if (x0<=0)
		x0 = 1;
	x1 = x+g_rgbSize;
	if (x1>=CAM_RES2_WIDTH)
		x1 = CAM_RES2_WIDTH-1;
	
	y0 = y-g_rgbSize;
	if (y0<=0)
		y0 = 1;
	y1 = y+g_rgbSize;
	if (y1>=CAM_RES2_HEIGHT)
		y1 = CAM_RES2_HEIGHT-1;
	
	for (i=y0, rsum=gsum=bsum=0; i<=y1; i++)
	{
		for (j=x0; j<=x1; j++)
		{
			interpolate(p, j, i, CAM_RES2_WIDTH, &r, &g, &b);
			rsum += r;
			gsum += g;
			bsum += b;
		}
	}
	d = (y1-y0+1)*(x1-x0+1);
	rsum /= d;
	gsum /= d;
	bsum /= d;
	
	rgb = rgbPack(r, g, b); 
	if (sat)
		return saturate(rgb);
	else
		return rgb;
}
void getGrayRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t sat, uint8_t strideX, uint8_t strideY, uint8_t* data, uint8_t length)
{	
	uint16_t x, y, i, j, k, sum, d, x_0, x_1, y_0, y_1;
	uint8_t r, g, b;
	uint8_t *p = (uint8_t *)SRAM1_LOC + CAM_PREBUF_LEN;
	
	x0 = MAX(0, MIN(x0, CAM_RES2_WIDTH - 3));
	y0 = MAX(0, MIN(y0, CAM_RES2_HEIGHT- 3));
	x1 = MAX(x0 + 1, MIN(x1, CAM_RES2_WIDTH -2));
	y1 = MAX(y0 + 1, MIN(y1, CAM_RES2_HEIGHT-2));
	
	for(x = x0, i = 0; x < x1 && i < length; x += strideX){
		for(y = y0; y < y1 && i < length; y += strideY, i++){
			
			//x_0 = x - g_rgbSize;			//x-2
			//y_0 = y - g_rgbSize;			//y-2
			//x_1 = x + g_rgbSize;			//x+2
			//y_1 = y + g_rgbSize;			//y+2
			
			//for (j = y_0, d = sum = 0; j < y_1; j++) {
			//	for (k=x_0; k < x_1; k++, d++) {
			//		interpolate(p, k, j, CAM_RES2_WIDTH, &r, &g, &b);
			//		sum += r + g + b;
			//	}
			//}
			
			interpolate(p, x, y, CAM_RES2_WIDTH, &r, &g, &b);
			data[i] = (r + g + b) / 3;
		}
	}
}

int ProgVideo::packet(uint8_t type, const uint8_t *data, uint8_t len, bool checksum)
{
	if (type==TYPE_REQUEST_GETRGB)
	{
		uint16_t x, y;
		uint32_t rgb;
		uint8_t saturate;

		if (len!=5)
		{
			ser_sendError(SER_ERROR_INVALID_REQUEST, checksum);
			return 0;
		}
		
		x = *(uint16_t *)(data+0);
		y = *(uint16_t *)(data+2);
		saturate = *(data+4);

		rgb = getRGB(x, y, saturate);
		ser_sendResult(rgb, checksum);
		
		return 0;
	}
	if (type==TYPE_REQUEST_GETGRAYRECT)
	{
		uint16_t x0, y0, x1, y1;								//je 2 zusammen 8
		uint8_t sat, strideX, strideY;					//je 1 zusammen 3
																						//zusammen 11 byte
		if (len!=11) {
			ser_sendError(SER_ERROR_INVALID_REQUEST, checksum);
			return 0;
		}
		
		x0 = *(uint16_t *)(data+0);
		y0 = *(uint16_t *)(data+2);
		x1 = *(uint16_t *)(data+4);
		y1 = *(uint16_t *)(data+6);
		sat = *(data+8);
		strideX = *(data+9);
		strideY = *(data+10);
		
		uint8_t outData[256];
		uint8_t length = MIN(((x1 - x0) / strideX) * ((y1 - y0) / strideY), 255);
		
		getGrayRect(x0,y0,x1,y1,sat,strideX, strideY, outData, length);
		ser_sendResults(outData, length, checksum);
		
		return 0;
	}
	
	// nothing rings a bell, return error
	return -1;
}


void ProgVideo::sendCustom(uint8_t renderFlags)
{
	uint32_t fourcc;

	// cooked mode
	if (g_execArg==1) 
		cam_sendFrame(g_chirpUsb, CAM_RES2_WIDTH, CAM_RES2_HEIGHT, RENDER_FLAG_FLUSH, FOURCC('C','M','V','2'));
	//  experimental mode, for new monmodules, etc.
	else if (100<=g_execArg && g_execArg<200) 
	{
		fourcc = FOURCC('E','X',((g_execArg%100)/10 + '0'), ((g_execArg%10) + '0'));
		cam_sendFrame(g_chirpUsb, CAM_RES2_WIDTH, CAM_RES2_HEIGHT, RENDER_FLAG_FLUSH, fourcc);
	}
	// undefined, just send plain raw frame (BA81)
	else 
		cam_sendFrame(g_chirpUsb, CAM_RES2_WIDTH, CAM_RES2_HEIGHT);

}



