#pragma once

#include "ofConstants.h"
#include "ofAppBaseWindow.h"
#include "ofEvents.h"

class ofBaseApp;
class ofVkRenderer;

class ofAppVkNoWindow : public ofAppBaseWindow {

public:
	ofAppVkNoWindow();
	~ofAppVkNoWindow(){}

	static bool doesLoop(){ return false; }
	static bool allowsMultiWindow(){ return false; }
	static void loop(){};
	static bool needsPolling(){ return false; }
	static void pollEvents(){};

	void run(ofBaseApp * appPtr);

	static void exitApp();
	void setup(const ofWindowSettings & settings);
	void setup( const ofVkWindowSettings& settings );

	void update();
	void draw();

	glm::vec2	getWindowPosition();
	glm::vec2	getWindowSize();
	glm::vec2	getScreenSize();

	int			getWidth();
	int			getHeight();

	ofCoreEvents & events();
	std::shared_ptr<ofBaseRenderer> & renderer();

private:
	int width, height;

    ofBaseApp *		ofAppPtr;
    ofCoreEvents coreEvents;
    std::shared_ptr<ofBaseRenderer> currentRenderer;
};
