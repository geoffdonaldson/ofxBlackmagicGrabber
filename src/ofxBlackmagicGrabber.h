/*
 * ofxBlackmagicGrabber.h
 *
 *  Created on: 06/10/2011
 *      Author: arturo
 */

#ifndef OFXBLACKMAGICGRABBER_H_
#define OFXBLACKMAGICGRABBER_H_

#include "DeckLinkAPI.h"
#include "ofMain.h"
#include "MSAOpenCL.h"


class ofxBlackmagicGrabber: public ofBaseVideoGrabber, public IDeckLinkInputCallback
{
public:
	ofxBlackmagicGrabber();
	~ofxBlackmagicGrabber();

	//specific blackmagic
	void setVideoMode(_BMDDisplayMode videoMode);

	virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame * videoFrame, IDeckLinkAudioInputPacket * audioFrame);
	virtual HRESULT QueryInterface(REFIID, void**){}
	virtual ULONG AddRef(){}
	virtual ULONG Release(){}
    
	// common ofBaseVideoGrabber
	void	listDevices();
	bool	initGrabber(int w, int h);
	void	update();
    void	close();

	bool	isFrameNew();

	unsigned char 	* getPixels();
	ofPixels & getPixelsRef();
    ofTexture & getTextureReference();
    
	float	getHeight();
	float	getWidth();

	void setVerbose(bool bTalkToMe);
	void setDeviceID(int _deviceID);
	void setDesiredFrameRate(int framerate);
	bool setPixelFormat(ofPixelFormat pixelFormat);
	ofPixelFormat getPixelFormat();

	static string LOG_NAME;

private:
	IDeckLink 				*deckLink;
	IDeckLinkInput			*deckLinkInput;
	IDeckLinkDisplayMode	*displayMode;
            
	BMDVideoInputFlags		inputFlags;
	BMDDisplayMode			selectedDisplayMode;
	BMDPixelFormat			pixelFormat;

	BMDTimecodeFormat		g_timecodeFormat;
	int						g_videoModeIndex;
	int						g_audioChannels;
	int						g_audioSampleDepth;

	unsigned long			frameCount;
    unsigned char *         yuvUChar;
    unsigned char *         rgbUChar;

	bool 					bNewFrameArrived;
	bool					bIsNewFrame;

	ofMutex					pixelsMutex;

	int						deviceID;
    
    msa::OpenCL             openCL;
    msa::OpenCLImage        rgbImage;
    msa::OpenCLBuffer       yuvImage;
    
};

#endif /* OFXBLACKMAGICGRABBER_H_ */
