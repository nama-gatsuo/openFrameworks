#pragma once

#include "vulkan/vulkan.hpp"
#include "vk/HelperTypes.h"

#include <vector>
#include <mutex>

namespace of{
namespace vk{

// ----------------------------------------------------------------------

struct SwapchainSettings
{
	uint32_t                width = 0;
	uint32_t                height = 0;
	uint32_t                numSwapchainImages = 0;
};

// ----------------------------------------------------------------------

struct WsiSwapchainSettings : public SwapchainSettings
{
	::vk::PresentModeKHR    presentMode = ::vk::PresentModeKHR::eFifo;
	::vk::SurfaceKHR        windowSurface = nullptr;
};


// ----------------------------------------------------------------------

// Todo: clarify this
// Image view for image which Swapchain donesn't own
// Owner of image is WSI
struct ImageWithView
{
	::vk::Image imageRef = nullptr;	   // owned by SwapchainKHR, only referenced here
	::vk::ImageView view = nullptr;
};

// ----------------------------------------------------------------------

class Swapchain {
public:

	virtual void setRendererProperties( const of::vk::RendererProperties& rendererProperties_ ) = 0 ;
	virtual void setup(){};

	virtual ~Swapchain(){};

	// Request an image index from the swapchain, so that we might render into it
	// the image must be returned to the swapchain when done using queuePresent
	// \note this might cause waiting.
	// presentCompleteSemaphore will be signalled once image is ready to be rendered into.
	virtual ::vk::Result acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t &imageIndex ) =  0;

	// Present the current image to the queue
	// Waits with execution until all waitSemaphores have been signalled
	virtual ::vk::Result queuePresent( ::vk::Queue queue, std::mutex & queueMutex,const std::vector<::vk::Semaphore>& waitSemaphores_ ) = 0;

	// return images vector
	// virtual const std::vector<ImageWithView> & getImages() const = 0 ;
	
	// return image by index
	virtual const ImageWithView& getImage( size_t i ) const = 0 ;

	// return number of swapchain images
	virtual const uint32_t getImageCount() const = 0;
	
	// return last acquired buffer id
	virtual const uint32_t & getCurrentImageIndex() const = 0;

	virtual const ::vk::Format& getColorFormat() = 0;

	// Return current swapchain image width in pixels
	virtual uint32_t getWidth() = 0 ;

	// Return current swapchain image height in pixels
	virtual uint32_t getHeight() = 0;

	// Change width and height in internal settings. 
	// Caution: this method requires a call to setup() to be applied, and is very costly.
	virtual void changeExtent( uint32_t w, uint32_t h ) = 0;
};

// ----------------------------------------------------------------------

class WsiSwapchain : public Swapchain
{
	const WsiSwapchainSettings mSettings;
	
	uint32_t             mImageCount = 0;
	uint32_t             mImageIndex = 0;

	std::vector<ImageWithView> mImages;  // owning, clients may only borrow!

	RendererProperties      mRendererProperties;
	const ::vk::Device      &mDevice = mRendererProperties.device;

	::vk::SwapchainKHR      mVkSwapchain;
	::vk::SurfaceFormatKHR  mWindowColorFormat = {};

	struct SurfaceProperties
	{
		bool queried = false;
		::vk::SurfaceCapabilitiesKHR        capabilities;
		std::vector<::vk::PresentModeKHR>   presentmodes;
		std::vector<::vk::SurfaceFormatKHR> surfaceFormats;
		VkBool32 presentSupported = VK_FALSE;
	} mSurfaceProperties;

	void                 querySurfaceCapabilities();
public:
	
	WsiSwapchain( const WsiSwapchainSettings& settings_ );
	
	void setRendererProperties( const of::vk::RendererProperties& rendererProperties_ ) override;

	void setup() override;

	virtual ~WsiSwapchain();

	// Request an image index from the swapchain, so that we might render into it
	// the image must be returned to the swapchain when done using queuePresent
	// \note this might cause waiting.
	::vk::Result acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t &imageIndex ) override;


	// Present the current image to the queue
	// Waits with execution until all waitSemaphores have been signalled
	// Pass renderComplete Semaphores here, so present waits for rendering to be complete.
	::vk::Result queuePresent( ::vk::Queue queue, std::mutex & queueMutex, const std::vector<::vk::Semaphore>& waitSemaphores_ ) override;

	// return image by index
	const ImageWithView& getImage( size_t i ) const override;

	// return number of swapchain images
	const uint32_t getImageCount() const override;

	// return last acquired buffer id
	const uint32_t & getCurrentImageIndex() const override;

	const ::vk::Format& getColorFormat() override;

	// Return current swapchain image width in pixels
	uint32_t getWidth() override;

	// Return current swapchain image height in pixels
	uint32_t getHeight() override;

	// Change width and height in internal settings. 
	// Caution: this method requires a call to setup() to be applied, and is very costly.
	void changeExtent( uint32_t w, uint32_t h ) override;

};


} // end namespace vk
} // end namespace of
