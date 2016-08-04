#pragma once

#include "ofMain.h"
#include "vk/vkTexture.h"

class ofApp : public ofBaseApp{

	ofEasyCam mCam1;
	ofMesh    mFontMesh;
	ofMesh	  mLMesh;

	std::shared_ptr<of::vk::Context> mExplicitContext;

	//of::vk::Texture mVkTex;

	public:
		void setup();
		void update();
		void draw();

		void drawModeExplicit();
		void drawModeMeshes();
		void drawModeLines();
		void drawModeSpinning();

		void keyPressed(int key);
		void keyReleased(int key);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void mouseEntered(int x, int y);
		void mouseExited(int x, int y);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);
		void exit();
};