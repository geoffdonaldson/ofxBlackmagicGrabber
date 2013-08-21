/*
 * ofxBlackmagicGrabber.cpp
 *
 *  Created on: 06/10/2011
 *      Author: arturo
 */
 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "ofxBlackmagicGrabber.h"
#include "image_formats.cl.h"

#include "Poco/ScopedLock.h"

string ofxBlackmagicGrabber::LOG_NAME="ofxBlackmagicGrabber";


// List of known pixel formats and their matching display names
static const BMDPixelFormat	gKnownPixelFormats[]		= {bmdFormat8BitYUV, bmdFormat10BitYUV, bmdFormat8BitARGB, bmdFormat8BitBGRA, bmdFormat10BitRGB, 0};
static const char *			gKnownPixelFormatNames[]	= {" 8-bit YUV", "10-bit YUV", "8-bit ARGB", "8-bit BGRA", "10-bit RGB", NULL};

ofxBlackmagicGrabber::ofxBlackmagicGrabber()
{
	inputFlags = 0;
	selectedDisplayMode = bmdModeNTSC;
	pixelFormat = bmdFormat8BitYUV;
	g_timecodeFormat = 0;
	g_videoModeIndex = -1;
	g_audioChannels = 2;
	g_audioSampleDepth = 16;
	bNewFrameArrived = false;
	bIsNewFrame = false;
	frameCount = 0;
	deckLinkInput = 0;
	deckLink = 0;
	deviceID = 0;
}

ofxBlackmagicGrabber::~ofxBlackmagicGrabber()
{
    close();
}

bool ofxBlackmagicGrabber::initGrabber(int w, int h)
{
	IDeckLinkIterator               *deckLinkIterator = CreateDeckLinkIteratorInstance();
	int                             displayModeCount = 0;
	int                             exitStatus = 1;
	bool                            foundDisplayMode = false;
	HRESULT                         result;
	IDeckLinkDisplayModeIterator	*displayModeIterator;
    
	if (!deckLinkIterator)
    {
		ofLogError(LOG_NAME) <<  "This application requires the DeckLink drivers installed.";
		goto bail;
	}
    
	for(int i=0;i<deviceID+1;i++)
    {
		result = deckLinkIterator->Next(&deckLink);
		if (result != S_OK)
        {
			ofLogError(LOG_NAME) <<  "Couldn't open device" << deviceID;
			goto bail;
		}
	}
    
	if (deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput) != S_OK)
		goto bail;
    
	deckLinkInput->SetCallback(this);
    
	// Obtain an IDeckLinkDisplayModeIterator to enumerate the display modes supported on output
	result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK)
    {
		ofLogError(LOG_NAME) << "Could not obtain the video output display mode iterator - result =" << result;
		goto bail;
	}
    
	if (g_videoModeIndex < 0)
    {
		ofLogError(LOG_NAME) <<  "No video mode specified, specify it before initGrabber using setVideoMode";
		goto bail;
	}
    
	while (displayModeIterator->Next(&displayMode) == S_OK)
    {
		if (g_videoModeIndex == displayMode->GetDisplayMode())
        {
			BMDDisplayModeSupport result;
			CFStringRef displayModeName = NULL;
            
			foundDisplayMode = true;
			displayMode->GetName(&displayModeName);
			selectedDisplayMode = displayMode->GetDisplayMode();
            
			ofLogVerbose(LOG_NAME) << "device initialized:" << displayMode->GetWidth() << displayMode->GetHeight();
            
			deckLinkInput->DoesSupportVideoMode(selectedDisplayMode, pixelFormat, bmdVideoInputFlagDefault, &result, NULL);
            
			if (result == bmdDisplayModeNotSupported){
				ofLogError(LOG_NAME) <<  "The display mode" << displayModeName << "is not supported with the selected pixel format";
				goto bail;
			}
            
			if (inputFlags & bmdVideoInputDualStream3D)
            {
				if (!(displayMode->GetFlags() & bmdDisplayModeSupports3D)){
					ofLogError(LOG_NAME) <<  "The display mode" << displayModeName  << "is not supported with 3D";
					goto bail;
				}
			}
            
			break;
		}
		displayModeCount++;
		displayMode->Release();
	}
    
    inputFlags  = bmdVideoInputEnableFormatDetection;
    
	if (!foundDisplayMode)
    {
		ofLogError(LOG_NAME) <<  "Invalid mode" << g_videoModeIndex << "specified";
		goto bail;
	}
    
	result = deckLinkInput->EnableVideoInput(selectedDisplayMode, pixelFormat, inputFlags);
	if(result != S_OK)
    {
		ofLogError(LOG_NAME) <<  "Failed to enable video input. Is another application using the card?";
		goto bail;
	}
    
#if 0  // no audio by now
	result = deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, g_audioSampleDepth, g_audioChannels);
	if(result != S_OK){
		goto bail;
	}
#endif
    
    
    openCL.setupFromOpenGL();
    rgbImage.initWithTexture(displayMode->GetWidth(), displayMode->GetHeight(), GL_RGBA);
	openCL.loadProgramFromSource(image_conversions);
    openCL.loadKernel("convert_b_yuyv_i_rgb");
    yuvImage.initBuffer(displayMode->GetWidth()*displayMode->GetHeight()*2);
    
    result = deckLinkInput->StartStreams();
	if(result != S_OK)
    {
		goto bail;
	}
    
	// All Okay.
	exitStatus = 0;
	return true;
    
bail:
    
	if (displayModeIterator != NULL){
		displayModeIterator->Release();
		displayModeIterator = NULL;
	}
    
	if (deckLinkIterator != NULL)
		deckLinkIterator->Release();
    
	close();
    
	return false;
}

void ofxBlackmagicGrabber::close()
{
    deckLinkInput->StopStreams();

	if (deckLinkInput != NULL){
		deckLinkInput->Release();
		deckLinkInput = NULL;
	}
    
	if (deckLink != NULL){
		deckLink->Release();
		deckLink = NULL;
	}
	frameCount = 0;
}

void ofxBlackmagicGrabber::update()
{
	if(bNewFrameArrived){
		bIsNewFrame = true;
		bNewFrameArrived = false;
	}else{
		bIsNewFrame = false;
	}
}

bool ofxBlackmagicGrabber::isFrameNew()
{
	return bIsNewFrame;
}


float ofxBlackmagicGrabber::getHeight()
{
	return rgbImage.getHeight();
}

float ofxBlackmagicGrabber::getWidth()
{
	return rgbImage.getWidth();
}

ofPixelFormat ofxBlackmagicGrabber::getPixelFormat()
{
	return OF_PIXELS_RGBA;
}

unsigned char * ofxBlackmagicGrabber::getPixels()
{
    return getPixelsRef().getPixels();
}

ofPixels & ofxBlackmagicGrabber::getPixelsRef()
{
    
    if (!pixels.isAllocated())
    {
        pixels.allocate(getWidth(), getHeight(), 4);
    }
    
    rgbImage.read(pixels.getPixels());
    
    return pixels;
}

ofTexture & ofxBlackmagicGrabber::getTextureReference()
{
    openCL.finish();
    return rgbImage.getTexture();
}

void ofxBlackmagicGrabber::setVideoMode(_BMDDisplayMode videoMode)
{
	g_videoModeIndex = videoMode;
}

void ofxBlackmagicGrabber::setDeviceID(int _deviceID)
{
	deviceID = _deviceID;
}

void ofxBlackmagicGrabber::setVerbose(bool bTalkToMe)
{
	if(bTalkToMe) ofSetLogLevel(LOG_NAME,OF_LOG_VERBOSE);
	else  ofSetLogLevel(LOG_NAME,OF_LOG_NOTICE);
}

void ofxBlackmagicGrabber::setDesiredFrameRate(int framerate)
{

}

bool ofxBlackmagicGrabber::setPixelFormat(ofPixelFormat pixelFormat)
{

}

#pragma mark - BMD Callbacks

HRESULT ofxBlackmagicGrabber::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode* displayMode, BMDDetectedVideoInputFormatFlags flags)
{
    BMDTimeValue num;
    BMDTimeScale den;
    
	displayMode->GetFrameRate(&num,&den);
	ofLogVerbose(LOG_NAME) << "video format changed" << displayMode->GetWidth() << "x" << displayMode->GetHeight() << "fps: " << num << "/" << den;
    
    // Stop the capture
    deckLinkInput->StopStreams();
    
    // Set the video input mode
    if (deckLinkInput->EnableVideoInput(displayMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection) != S_OK)
    {
        ofLogVerbose(LOG_NAME) << "Error restarting the capture. This application was unable to select the new video mode.";
        goto bail;
    }
    
    // Start the capture
    if (deckLinkInput->StartStreams() != S_OK)
    {
        ofLogVerbose(LOG_NAME) << "Error restarting the capture. This application was unable to start the capture on the selected device.";
        goto bail;
    }
    
bail:
	return S_OK;
}

HRESULT ofxBlackmagicGrabber::VideoInputFrameArrived(IDeckLinkVideoInputFrame * videoFrame, IDeckLinkAudioInputPacket * audioFrame)
{
	// Handle Video Frame
	if(videoFrame)
	{
		if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
        {
			ofLogError(LOG_NAME) <<  "Frame received (#" << frameCount << ") - No input signal detected";
		}
		else
		{
			CFStringRef  timecodeString = NULL;
            char    timeCode[64];
            
			if (g_timecodeFormat != 0)
			{
				IDeckLinkTimecode *timecode;
				if (videoFrame->GetTimecode(g_timecodeFormat, &timecode) == S_OK)
				{
					if (timecode->GetString(&timecodeString) == S_OK)
                    {
                        CFStringGetCString(timecodeString, timeCode, sizeof(timeCode), kCFStringEncodingMacRoman);
                        CFRelease(timecodeString);
                        
                    }else
                    {
                        timeCode[0] = ' ';
                        timeCode[0] = '\0';
                    }
                    
				}
			}
            
			ofLogVerbose(LOG_NAME) << "Frame received (#" <<  frameCount
            << ") [" << (timecodeString != NULL ? timeCode : "No timecode")
            << "] - Valid Frame - Size: " << (videoFrame->GetRowBytes() * videoFrame->GetHeight()) << " bytes";
            
            videoFrame->GetBytes((void**)&yuvUChar);
            yuvImage.write(yuvUChar, 0, displayMode->GetWidth()*displayMode->GetHeight()*2);
            
            uint w = videoFrame->GetWidth();
            uint h = videoFrame->GetHeight();
            
            //pixelsMutex.lock();
            
            msa::OpenCLKernel *kernel = openCL.kernel("convert_b_yuyv_i_rgb");
			kernel->setArg(0, yuvImage.getCLMem());
			kernel->setArg(1, w);
            kernel->setArg(2, h);
			kernel->setArg(3, rgbImage.getCLMem());
			kernel->run2D(videoFrame->GetWidth(), videoFrame->GetHeight());
            
            //pixelsMutex.unlock();
            
			bNewFrameArrived = true;
        }
        
		frameCount++;
	}
    
#if 0	//No audio
	// Handle Audio Frame
	void*	audioFrameBytes;
	if (audioFrame)
	{
		if (audioOutputFile != -1)
		{
			audioFrame->GetBytes(&audioFrameBytes);
			write(audioOutputFile, audioFrameBytes, audioFrame->GetSampleFrameCount() * g_audioChannels * (g_audioSampleDepth / 8));
		}
	}
#endif
	return S_OK;
}


#pragma mark - BMD Information




static void	print_attributes (IDeckLink* deckLink)
{
	IDeckLinkAttributes*				deckLinkAttributes = NULL;
	bool								supported;
	int64_t								count;
	CFStringRef                         serialPortName = NULL;
	HRESULT								result;
    
	// Query the DeckLink for its attributes interface
	result = deckLink->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not obtain the IDeckLinkAttributes interface - result = %08ld\n", result);
		goto bail;
	}
    
	// List attributes and their value
	printf("Attribute list:\n");
    
	result = deckLinkAttributes->GetFlag(BMDDeckLinkHasSerialPort, &supported);
	if (result == S_OK)
	{
		printf(" %-40s %s\n", "Serial port present ?", (supported == true) ? "Yes" : "No");
        
		if (supported)
		{
			result = deckLinkAttributes->GetString(BMDDeckLinkSerialPortDeviceName, &serialPortName);
			if (result == S_OK)
			{
                char	portName[64];
				CFStringGetCString(serialPortName, portName, sizeof(portName), kCFStringEncodingMacRoman);
                
				printf(" %-40s %s\n", "Serial port name: ", portName);
				CFRelease(serialPortName);
                
			}
			else
			{
				fprintf(stderr, "Could not query the serial port presence attribute- result = %08ld\n", result);
			}
		}
        
	}
	else
	{
		fprintf(stderr, "Could not query the serial port presence attribute- result = %08ld\n", result);
	}
    
    result = deckLinkAttributes->GetInt(BMDDeckLinkNumberOfSubDevices, &count);
    if (result == S_OK)
    {
        printf(" %-40s %lld\n", "Number of sub-devices:",  count);
        if (count != 0)
        {
            result = deckLinkAttributes->GetInt(BMDDeckLinkSubDeviceIndex, &count);
            if (result == S_OK)
            {
                printf(" %-40s %lld\n", "Sub-device index:",  count);
            }
            else
            {
                fprintf(stderr, "Could not query the sub-device index attribute- result = %08ld\n", result);
            }
        }
    }
    else
    {
        fprintf(stderr, "Could not query the number of sub-device attribute- result = %08ld\n", result);
    }
    
	result = deckLinkAttributes->GetInt(BMDDeckLinkMaximumAudioChannels, &count);
	if (result == S_OK)
	{
		printf(" %-40s %lld\n", "Number of audio channels:",  count);
	}
	else
	{
		fprintf(stderr, "Could not query the number of supported audio channels attribute- result = %08ld\n", result);
	}
    
	result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supported);
	if (result == S_OK)
	{
		printf(" %-40s %s\n", "Input mode detection supported ?", (supported == true) ? "Yes" : "No");
	}
	else
	{
		fprintf(stderr, "Could not query the input mode detection attribute- result = %08ld\n", result);
	}
    
	result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInternalKeying, &supported);
	if (result == S_OK)
	{
		printf(" %-40s %s\n", "Internal keying supported ?", (supported == true) ? "Yes" : "No");
	}
	else
	{
		fprintf(stderr, "Could not query the internal keying attribute- result = %08ld\n", result);
	}
    
	result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsExternalKeying, &supported);
	if (result == S_OK)
	{
		printf(" %-40s %s\n", "External keying supported ?", (supported == true) ? "Yes" : "No");
	}
	else
	{
		fprintf(stderr, "Could not query the external keying attribute- result = %08ld\n", result);
	}
    
	result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsHDKeying, &supported);
	if (result == S_OK)
	{
		printf(" %-40s %s\n", "HD-mode keying supported ?", (supported == true) ? "Yes" : "No");
	}
	else
	{
		fprintf(stderr, "Could not query the HD-mode keying attribute- result = %08ld\n", result);
	}
    
bail:
	printf("\n");
	if(deckLinkAttributes != NULL)
		deckLinkAttributes->Release();
    
}

static void	print_output_modes (IDeckLink* deckLink)
{
	IDeckLinkOutput*					deckLinkOutput = NULL;
	IDeckLinkDisplayModeIterator*		displayModeIterator = NULL;
	IDeckLinkDisplayMode*				displayMode = NULL;
	HRESULT								result;
    
	// Query the DeckLink for its configuration interface
	result = deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&deckLinkOutput);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not obtain the IDeckLinkOutput interface - result = %08ld\n", result);
		goto bail;
	}
    
	// Obtain an IDeckLinkDisplayModeIterator to enumerate the display modes supported on output
	result = deckLinkOutput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not obtain the video output display mode iterator - result = %08ld\n", result);
		goto bail;
	}
    
	// List all supported output display modes
	printf("Supported video output display modes and pixel formats:\n");
	while (displayModeIterator->Next(&displayMode) == S_OK)
	{
		CFStringRef			displayModeString = NULL;
        
		result = displayMode->GetName(&displayModeString);
		if (result == S_OK)
		{
			char					modeName[64];
			int						modeWidth;
			int						modeHeight;
			BMDTimeValue			frameRateDuration;
			BMDTimeScale			frameRateScale;
			int						pixelFormatIndex = 0; // index into the gKnownPixelFormats / gKnownFormatNames arrays
			BMDDisplayModeSupport	displayModeSupport;
            
            
			// Obtain the display mode's properties
			modeWidth = displayMode->GetWidth();
			modeHeight = displayMode->GetHeight();
			displayMode->GetFrameRate(&frameRateDuration, &frameRateScale);
            
            CFStringGetCString(displayModeString, modeName, sizeof(modeName), kCFStringEncodingMacRoman);
            
			printf(" %-20s \t %d x %d \t %7g FPS\t", modeName, modeWidth, modeHeight, (double)frameRateScale / (double)frameRateDuration);
            
			// Print the supported pixel formats for this display mode
			while ((gKnownPixelFormats[pixelFormatIndex] != 0) && (gKnownPixelFormatNames[pixelFormatIndex] != NULL))
			{
				if ((deckLinkOutput->DoesSupportVideoMode(displayMode->GetDisplayMode(), gKnownPixelFormats[pixelFormatIndex], bmdVideoOutputFlagDefault, &displayModeSupport, NULL) == S_OK)
                    && (displayModeSupport != bmdDisplayModeNotSupported))
				{
					printf("%s\t", gKnownPixelFormatNames[pixelFormatIndex]);
				}
				pixelFormatIndex++;
			}
            
			printf("\n");
            
			CFRelease(displayModeString);
		}
        
		// Release the IDeckLinkDisplayMode object to prevent a leak
		displayMode->Release();
	}
    
	printf("\n");
    
bail:
	// Ensure that the interfaces we obtained are released to prevent a memory leak
	if (displayModeIterator != NULL)
		displayModeIterator->Release();
    
	if (deckLinkOutput != NULL)
		deckLinkOutput->Release();
}


static void	print_capabilities (IDeckLink* deckLink)
{
	IDeckLinkAttributes*		deckLinkAttributes = NULL;
	int64_t						ports;
	int							itemCount;
	HRESULT						result;
    
	// Query the DeckLink for its configuration interface
	result = deckLink->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not obtain the IDeckLinkAttributes interface - result = %08ld\n", result);
		goto bail;
	}
    
	printf("Supported video output connections:\n  ");
	itemCount = 0;
	result = deckLinkAttributes->GetInt(BMDDeckLinkVideoOutputConnections, &ports);
	if (result == S_OK)
	{
		if (ports & bmdVideoConnectionSDI)
		{
			itemCount++;
			printf("SDI");
		}
        
		if (ports & bmdVideoConnectionHDMI)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("HDMI");
		}
        
		if (ports & bmdVideoConnectionOpticalSDI)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Optical SDI");
		}
        
		if (ports & bmdVideoConnectionComponent)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Component");
		}
        
		if (ports & bmdVideoConnectionComposite)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Composite");
		}
        
		if (ports & bmdVideoConnectionSVideo)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("S-Video");
		}
	}
	else
	{
		fprintf(stderr, "Could not obtain the list of output ports - result = %08ld\n", result);
		goto bail;
	}
    
	printf("\n\n");
    
	printf("Supported video input connections:\n  ");
	itemCount = 0;
	result = deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &ports);
	if (result == S_OK)
	{
		if (ports & bmdVideoConnectionSDI)
		{
			itemCount++;
			printf("SDI");
		}
        
		if (ports & bmdVideoConnectionHDMI)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("HDMI");
		}
        
		if (ports & bmdVideoConnectionOpticalSDI)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Optical SDI");
		}
        
		if (ports & bmdVideoConnectionComponent)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Component");
		}
        
		if (ports & bmdVideoConnectionComposite)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("Composite");
		}
        
		if (ports & bmdVideoConnectionSVideo)
		{
			if (itemCount++ > 0)
				printf(", ");
			printf("S-Video");
		}
	}
	else
	{
		fprintf(stderr, "Could not obtain the list of input ports - result = %08ld\n", result);
		goto bail;
	}
	printf("\n");
    
bail:
	if (deckLinkAttributes != NULL)
		deckLinkAttributes->Release();
}

void ofxBlackmagicGrabber::listDevices()
{
	IDeckLinkIterator*		deckLinkIterator;
	IDeckLink*				deckLink;
	int						numDevices = 0;
	HRESULT					result;
    
	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (deckLinkIterator == NULL)
    {
		ofLogError(LOG_NAME) <<  "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.";
	}
    
	// Enumerate all cards in this system
	while (deckLinkIterator->Next(&deckLink) == S_OK)
    {
		CFStringRef deviceNameString = NULL;
		// Increment the total number of DeckLink cards found
		numDevices++;
		if (numDevices > 1)
			printf("\n\n");
        
		// *** Print the model name of the DeckLink card
		result = deckLink->GetModelName(&deviceNameString);
		if (result == S_OK)
		{
            char   deviceName[64];
            CFStringGetCString(deviceNameString, deviceName, sizeof(deviceName), kCFStringEncodingMacRoman);
			printf("=============== %s ===============\n\n", deviceName);
            CFRelease(deviceNameString);
		}
        
		print_attributes(deckLink);
        
		// ** List the video output display modes supported by the card
		print_output_modes(deckLink);
        
		// ** List the input and output capabilities of the card
		print_capabilities(deckLink);
        
		// Release the IDeckLink instance when we've finished with it to prevent leaks
		deckLink->Release();
	}
    
	deckLinkIterator->Release();
    
	// If no DeckLink cards were found in the system, inform the user
	if (numDevices == 0)
		ofLogError(LOG_NAME) <<  "No Blackmagic Design devices were found.";
    
}


bool determinePorts(DECKLINK_CARD_TYPE type, int * portList)
{
	IDeckLinkIterator*		deckLinkIterator;
	IDeckLink*				deckLink;
	int						numDevices = 0;
    int                     portIndex = 0;
	HRESULT					result;
    bool                    found = false;
    
	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (deckLinkIterator == NULL)
    {
		cout <<  "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.";
	}
    
	// Enumerate all cards in this system
	while (deckLinkIterator->Next(&deckLink) == S_OK)
    {
		CFStringRef deviceNameString = NULL;
		// Increment the total number of DeckLink cards found
		numDevices++;
        
		// *** Print the model name of the DeckLink card        
        
        IDeckLinkAttributes*				deckLinkAttributes = NULL;
        bool								supported;
        int64_t								count;
        int64_t                             index;
        CFStringRef                         serialPortName = NULL;
        HRESULT								result;
        
        // Query the DeckLink for its attributes interface
        result = deckLink->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
        if (result != S_OK)
        {
            fprintf(stderr, "Could not obtain the IDeckLinkAttributes interface - result = %08ld\n", result);
            goto bail;
        }

        result = deckLinkAttributes->GetInt(BMDDeckLinkNumberOfSubDevices, &count);
        if (result == S_OK)
        {
            if (count != 0 & count == type)
            {                
                result = deckLinkAttributes->GetInt(BMDDeckLinkSubDeviceIndex, &index);
                if (result == S_OK)
                {                    
                    portList[index] = numDevices - 1;
                    
                    result = deckLink->GetModelName(&deviceNameString);
                    if (result == S_OK)
                    {
                        char   deviceName[64];
                        CFStringGetCString(deviceNameString, deviceName, sizeof(deviceName), kCFStringEncodingMacRoman);
                        printf("\n\n=============== %s ===============\n\n", deviceName);
                        CFRelease(deviceNameString);
                    }

                    printf(" %-40s %lld\n", "Number of sub-devices:",  count);
                    printf(" %-40s %lld\n", "Sub-device index:",  index);
                    
                    found = true;
                    
                }
                else
                {
                    fprintf(stderr, "Could not query the sub-device index attribute- result = %08ld\n", result);
                }
            }
        }
        else
        {
            fprintf(stderr, "Could not query the number of sub-device attribute- result = %08ld\n", result);
        }
        
        bail:
            if(deckLinkAttributes != NULL)
                deckLinkAttributes->Release();

        deckLink->Release();
	}
    
    // Release the IDeckLink instance when we've finished with it to prevent leaks
	deckLinkIterator->Release();
    
	// If no DeckLink cards were found in the system, inform the user
	if (!found)
    {
		cout <<  "No Blackmagic Design devices were found for type [" << type << "]. " <<  endl;
        return false;
    }
    
    return true;
    
}



