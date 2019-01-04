#pragma once

#include "ofConstants.h"

#ifdef OF_TARGET_API_VULKAN
	#define GLFW_INCLUDE_VULKAN
#else
	#define GLFW_INCLUDE_NONE
#endif
#include "GLFW/glfw3.h"

#include "ofAppBaseWindow.h"
#include "ofRectangle.h"

#ifdef TARGET_LINUX
typedef struct _XIM * XIM;
typedef struct _XIC * XIC;
#endif

class ofBaseApp;
struct GLFWwindow;
class ofCoreEvents;
template<typename T>
class ofPixels_;
typedef ofPixels_<unsigned char> ofPixels;

#ifdef TARGET_OPENGLES
class ofGLFWWindowSettings: public ofGLESWindowSettings{
#elif defined(OF_TARGET_API_VULKAN)
class ofGLFWWindowSettings : public ofVkWindowSettings{
#else
class ofGLFWWindowSettings: public ofGLWindowSettings{
#endif
public:
	ofGLFWWindowSettings(){}

#ifdef TARGET_OPENGLES
	ofGLFWWindowSettings(const ofGLESWindowSettings & settings)
		: ofGLESWindowSettings(settings){}
#elif defined(OF_TARGET_API_VULKAN)
	ofGLFWWindowSettings( const ofVkWindowSettings & settings )
		: ofVkWindowSettings( settings ){}
#else
	ofGLFWWindowSettings(const ofGLWindowSettings & settings)
		: ofGLWindowSettings(settings){}
#endif

#ifndef OF_TARGET_API_VULKAN
	int numSamples = 4;
	bool doubleBuffering = true;
	int redBits = 8;
	int greenBits = 8;
	int blueBits = 8;
	int alphaBits = 8;
	int depthBits = 24;
	int stencilBits = 0;
	bool stereo = false;
	shared_ptr<ofAppBaseWindow> shareContextWith;
#endif
	bool multiMonitorFullScreen = false;
	bool visible = true;
	bool iconified = false;
	bool decorated = true;
	bool resizable = true;
	int monitor = 0;
};

#if defined(TARGET_OPENGLES)
class ofAppGLFWWindow : public ofAppBaseGLESWindow{
#elif defined(OF_TARGET_API_VULKAN)
class ofAppGLFWWindow : public ofAppBaseVkWindow {
#else
class ofAppGLFWWindow : public ofAppBaseGLWindow {
#endif

public:

	ofAppGLFWWindow();
	~ofAppGLFWWindow();

	// Can't be copied, use shared_ptr
	ofAppGLFWWindow(ofAppGLFWWindow & w) = delete;
	ofAppGLFWWindow & operator=(ofAppGLFWWindow & w) = delete;

	static void loop(){};
	static bool doesLoop(){ return false; }
	static bool allowsMultiWindow(){ return true; }
	static bool needsPolling(){ return true; }
	static void pollEvents();


    // this functions are only meant to be called from inside OF don't call them from your code
    using ofAppBaseWindow::setup;
#ifdef TARGET_OPENGLES
	void setup(const ofGLESWindowSettings & settings);
#elif defined(OF_TARGET_API_VULKAN)

	void setup( const ofVkWindowSettings & settings );

	// Create a vkSurface using GLFW. The surface is owned by the current window.
	VkResult             createVkSurface();

	// storage for a pointer to a vkSurface owned by this window
	VkSurfaceKHR         mWindowSurface;

	// Destroy a vkSurface
	void destroyVkSurface();

	// Return vkSurface used to render to this window
	const VkSurfaceKHR&  getVkSurface();


#else
	void setup(const ofGLWindowSettings & settings);
#endif
	void setup(const ofGLFWWindowSettings & settings);
	void update();
	void draw();
	bool getWindowShouldClose();
	void setWindowShouldClose();

	void hideCursor();
	void showCursor();

	int getHeight();
	int getWidth();

	ofCoreEvents & events();
	std::shared_ptr<ofBaseRenderer> & renderer();

    GLFWwindow* getGLFWWindow();
    void * getWindowContext(){return getGLFWWindow();}
	ofGLFWWindowSettings getSettings(){ return settings; }

	glm::vec2	getWindowSize();
	glm::vec2	getScreenSize();
	glm::vec2 	getWindowPosition();

	void setWindowTitle(std::string title);
	void setWindowPosition(int x, int y);
	void setWindowShape(int w, int h);

	void			setOrientation(ofOrientation orientation);
	ofOrientation	getOrientation();

	ofWindowMode	getWindowMode();

	void		setFullscreen(bool fullscreen);
	void		toggleFullscreen();

	void		enableSetupScreen();
	void		disableSetupScreen();

	void		setVerticalSync(bool bSync);

    void        setClipboardString(const std::string& text);
    std::string      getClipboardString();

    int         getPixelScreenCoordScale();

#ifndef OF_TARGET_API_VULKAN
    void 		makeCurrent();
	void swapBuffers();
#endif

	void startRender();
	void finishRender();

	static void listVideoModes();
	static void listMonitors();
	bool isWindowIconified();
	bool isWindowActive();
	bool isWindowResizeable();
	void iconify(bool bIconify);

#if defined(TARGET_LINUX) && !defined(TARGET_RASPBERRY_PI)
	Display* 	getX11Display();
	Window  	getX11Window();
	XIC			getX11XIC();
#endif

#if defined(TARGET_LINUX) && !defined(TARGET_OPENGLES)
	GLXContext 	getGLXContext();
#endif

#if defined(TARGET_LINUX) && defined(TARGET_OPENGLES)
	EGLDisplay 	getEGLDisplay();
	EGLContext 	getEGLContext();
	EGLSurface 	getEGLSurface();
#endif

#if defined(TARGET_OSX)
	void *		getNSGLContext();
	void *		getCocoaWindow();
#endif

#if defined(TARGET_WIN32)
	HGLRC 		getWGLContext();
	HWND 		getWin32Window();
#endif

private:
	static ofAppGLFWWindow * setCurrent(GLFWwindow* windowP);
	static void 	mouse_cb(GLFWwindow* windowP_, int button, int state, int mods);
	static void 	motion_cb(GLFWwindow* windowP_, double x, double y);
	static void 	entry_cb(GLFWwindow* windowP_, int entered);
	static void 	keyboard_cb(GLFWwindow* windowP_, int key, int scancode, int action, int mods);
	static void 	char_cb(GLFWwindow* windowP_, uint32_t key);
	static void 	resize_cb(GLFWwindow* windowP_, int w, int h);
	static void 	framebuffer_size_cb(GLFWwindow* windowP_, int w, int h);
	static void 	exit_cb(GLFWwindow* windowP_);
	static void		scroll_cb(GLFWwindow* windowP_, double x, double y);
	static void 	drop_cb(GLFWwindow* windowP_, int numFiles, const char** dropString);
	static void		error_cb(int errorCode, const char* errorDescription);

	void close();

#ifdef TARGET_LINUX
	void setWindowIcon(const std::string & path);
	void setWindowIcon(const ofPixels & iconPixels);
	XIM xim;
	XIC xic;
#endif

	std::unique_ptr<ofCoreEvents> coreEvents;
	std::shared_ptr<ofBaseRenderer> currentRenderer;
	ofGLFWWindowSettings settings;

	ofWindowMode	targetWindowMode;

	bool			bEnableSetupScreen;
	int				windowW, windowH;		/// Physical framebuffer pixels extents
	int				currentW, currentH;		/// Extents of the window client area, which may be scaled by pixelsScreenCoordScale to map to physical framebuffer pixels.
	float           pixelScreenCoordScale;  /// Scale factor from virtual operating-system defined client area extents (as seen in currentW, currentH) to physical framebuffer pixel coordinates (as seen in windowW, windowH).

	ofRectangle windowRect;

	int				buttonInUse;
	bool			buttonPressed;

	int 			nFramesSinceWindowResized;
	bool			bWindowNeedsShowing;

	GLFWwindow* 	windowP;

	int				getCurrentMonitor();

	ofBaseApp *	ofAppPtr;


	ofOrientation orientation;

	bool iconSet;

    #ifdef TARGET_WIN32
    LONG lExStyle, lStyle;
    #endif // TARGET_WIN32
};


//#endif
