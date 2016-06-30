#include "vk/ofVkRenderer.h"
#include "vk/Pipeline.h"
#include "vk/Shader.h"
#include "vk/vkUtils.h"

#include "spirv_cross.hpp"
#include <algorithm>

// ----------------------------------------------------------------------

void ofVkRenderer::setup(){

	// the surface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	querySurfaceCapabilities();
	// vkprepare:
	createCommandPool();
	
	createSetupCommandBuffer();
	{
		setupSwapChain();
		createCommandBuffers();
		setupDepthStencil();
		// TODO: let's make sure that this is more explicit,
		// and that you can set up your own render passes.
		setupRenderPass();

		// here we create a pipeline cache so that we can create a pipeline from it in preparePipelines
		mPipelineCache = of::vk::createPipelineCache(mDevice,"testPipelineCache.bin");

		mViewport = { 0.f, 0.f, float( mWindowWidth ), float( mWindowHeight ) };
		setupFrameBuffer();
	}
	// submit, then free the setupCommandbuffer.
	flushSetupCommandBuffer();
	
	createSemaphores();

	// Set up as many Contexts as swapchains.
	// A context holds dynamic frame state + manages GPU memory for "immediate" mode
	
	
	mContext = make_shared<of::vk::Context>();
	mContext->setup( this, mSwapchain.getImageCount() );
	

	// shaders will let us know about descriptorSetLayouts.
	setupShaders();

	// create a descriptor pool from which descriptor sets can be allocated
	setupDescriptorPool();

	// once we know the layout for the descriptorSets, we
	// can allocate them from the pool based on the layout
	// information
	setupDescriptorSets();

	setupPipelines();					  
	
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorSets(){
	
	// descriptor sets are there to describe how uniforms are fed to a pipeline

	// descriptor set is allocated from pool mDescriptorPool
	// based on information from descriptorSetLayout which was derived from shader code reflection 
	// 
	// a descriptorSetLayout describes a descriptor set, it tells us the 
	// number and ordering of descriptors within the set.
	
	{
		std::vector<VkDescriptorSetLayout> dsl( mDescriptorSetLayouts.size() );
		for ( size_t i = 0; i != dsl.size(); ++i ){
			dsl[i] = *mDescriptorSetLayouts[i];
		}

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mDescriptorPool;		              // pool  : tells us where to allocate from
		allocInfo.descriptorSetCount = dsl.size();  // count : tells us how many descriptor set layouts 
		allocInfo.pSetLayouts = dsl.data();         // layout: tells us how many descriptors, and how these are laid out 
		allocInfo.pNext = VK_NULL_HANDLE;

		mDescriptorSets.resize( mDescriptorSetLayouts.size() );
		vkAllocateDescriptorSets( mDevice, &allocInfo, mDescriptorSets.data() );	// allocates mDescriptorSets
	}
	
	// At this point the descriptors within the set are untyped 
	// so we have to write type information into it, 
	// as well as binding information so the set knows how to ingest data from memory
	
	// TODO: write descriptor information to all *unique* bindings over all shaders
	// make sure to re-use descriptors for shared bindings.

	// get bindings from shader
	auto bindings = mShaders[0]->getBindings();

	std::vector<VkWriteDescriptorSet> writeDescriptorSets(bindings.size());

	{
		// Careful! bufferInfo must be retrieved from somewhere... 
		// this means probably that we shouldn't write to our 
		// descriptors before we know the buffer that is going to 
		// be used with them.

		// TODO: query context for matching descriptor set 
		// binding name -> match default Uniform to default uniform for example!

		size_t i = 0;
		for ( auto &b : bindings ){
			writeDescriptorSets[i] = {
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
				nullptr,                                                   // const void*                      pNext;
				mDescriptorSets[0],                       //<-- check      // VkDescriptorSet                  dstSet;
				b.second.layout.binding,                  //<-- check      // uint32_t                         dstBinding;
				0,                                                         // uint32_t                         dstArrayElement;
				1,                                                         // uint32_t                         descriptorCount;
				b.second.layout.descriptorType,           //<-- check      // VkDescriptorType                 descriptorType;
				nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
				&mContext->getDescriptorBufferInfo(),                      // const VkDescriptorBufferInfo*    pBufferInfo;
				nullptr,                                                   // const VkBufferView*              pTexelBufferView;

			};
		}
		++i;
	}

	vkUpdateDescriptorSets( mDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL );	 
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorPool(){
	// descriptors are allocated from a per-thread pool
	// the pool needs to reserve size based on the 
	// maximum number for each type of descriptor

	// list of all descriptors types and their count
	std::vector<VkDescriptorPoolSize> typeCounts;

	uint32_t maxSets = 0;

	// iterate over descriptorsetlayouts to find out what we need
	// and to populate list above
	{	
		// count all necessary descriptor of all necessary types over
		// all currently known shaders.
		std::map<VkDescriptorType, uint32_t> descriptorTypes;
		
		for ( const auto & s : mShaders ){
			for ( const auto & b : s->getBindings() ){
				if ( descriptorTypes.find( b.second.layout.descriptorType ) == descriptorTypes.end() ){
					// first of this kind
					descriptorTypes[b.second.layout.descriptorType] = 1;
				}
				else{
					++descriptorTypes[b.second.layout.descriptorType];
				}
			}
		}
			
		// accumulate total number of descriptor sets
		// TODO: find out: is this the max number of descriptor sets or the max number of descriptors?
		for ( const auto &t : descriptorTypes ){
			typeCounts.push_back( {t.first, t.second} );
			maxSets += t.second;
		}

	}

	// Create the global descriptor pool
	// All descriptors used in this example are allocated from this pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,             // VkStructureType                sType;
		nullptr,                                                   // const void*                    pNext;
		0,                                                         // VkDescriptorPoolCreateFlags    flags;
		maxSets,                                                   // uint32_t                       maxSets;
		typeCounts.size(),                                         // uint32_t                       poolSizeCount;
		typeCounts.data(),                                         // const VkDescriptorPoolSize*    pPoolSizes;
	};

	VkResult vkRes = vkCreateDescriptorPool( mDevice, &descriptorPoolInfo, nullptr, &mDescriptorPool );
	assert( !vkRes );
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupShaders(){
	// -- load shaders

	of::vk::Shader::Settings settings{
		mDevice,
		{
			{ VK_SHADER_STAGE_VERTEX_BIT  , "vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "frag.spv" },
		}
	};

	auto shader = std::make_shared<of::vk::Shader>( settings );
	mShaders.emplace_back( shader );
	auto descriptorSetLayout = shader->createDescriptorSetLayout();
	mDescriptorSetLayouts.emplace_back( descriptorSetLayout );

	// create temporary object which may be borrowed by createPipeline method
	std::vector<VkDescriptorSetLayout> dsl( mDescriptorSetLayouts.size() );
	// fill with elements borrowed from mDescriptorSets	
	std::transform( mDescriptorSetLayouts.begin(), mDescriptorSetLayouts.end(), dsl.begin(), 
		[]( auto & lhs )->VkDescriptorSetLayout { return *lhs; } );

	auto pl = of::vk::createPipelineLayout(mDevice, dsl );

	mPipelineLayouts.emplace_back( pl );

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupPipelines(){

	// GraphicsPipelineState comes with sensible defaults
	// and is able to produce pipelines based on its current state.
	// the idea will be to a dynamic version of this object to
	// keep track of current context state and create new pipelines
	// on the fly if needed, or, alternatively, create all pipeline
	// combinatinons upfront based on a .json file which lists each
	// state combination for required pipelines.
	of::vk::GraphicsPipelineState defaultPSO;

	// TODO: let us choose which shader we want to use with our pipeline.
	defaultPSO.mShader           = mShaders[0];
	defaultPSO.mRenderPass       = mRenderPass;
	defaultPSO.mLayout           = mPipelineLayouts[0];
	
	mPipelines.solid = defaultPSO.createPipeline( mDevice, mPipelineCache );
	
	defaultPSO.mRasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
	mPipelines.wireframe = defaultPSO.createPipeline( mDevice, mPipelineCache );
	
}
 
// ----------------------------------------------------------------------

void ofVkRenderer::createSemaphores(){
	
	VkSemaphoreCreateInfo semaphoreCreateInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType           sType;
		nullptr,                                 // const void*               pNext;
		0,                                       // VkSemaphoreCreateFlags    flags;
	};

	// This semaphore ensures that the image is complete before starting to submit again
	vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphorePresentComplete );

	// This semaphore ensures that all commands submitted 
	// have been finished before submitting the image to the queue
	vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphoreRenderComplete );
}

// ----------------------------------------------------------------------

void ofVkRenderer::querySurfaceCapabilities(){

	// we need to find out if the current physical device supports 
	// PRESENT
	
	VkBool32 presentSupported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR( mPhysicalDevice, mVkGraphicsFamilyIndex, mWindowSurface, &presentSupported );

	// find out which color formats are supported

	// Get list of supported surface formats
	uint32_t formatCount;
	auto err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, NULL );

	if ( err != VK_SUCCESS || formatCount == 0 ){
		ofLogError() << "Vulkan error: No valid format was found.";
		ofExit( 1 );
	}

	std::vector<VkSurfaceFormatKHR> surfaceFormats( formatCount );
	err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, surfaceFormats.data() );
	assert( !err );

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ( ( formatCount == 1 ) && ( surfaceFormats[0].format == VK_FORMAT_UNDEFINED ) ){
		mWindowColorFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else{
		// Always select the first available color format
		// If you need a specific format (e.g. SRGB) you'd need to
		// iterate over the list of available surface format and
		// check for its presence
		mWindowColorFormat.format = surfaceFormats[0].format;
	}
	mWindowColorFormat.colorSpace = surfaceFormats[0].colorSpace;

	ofLog() << "Present supported: " << ( presentSupported ? "TRUE" : "FALSE" );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandPool(){
	// create a command pool
	VkCommandPoolCreateInfo poolInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // VkStructureType             sType;
		nullptr,                                         // const void*                 pNext;
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // VkCommandPoolCreateFlags    flags;
		0,                                               // uint32_t                    queueFamilyIndex;
	};
		 
	// VkCommandPoolCreateFlags --> tells us how persistent the commands living in this pool are going to be
	vkCreateCommandPool( mDevice, &poolInfo, nullptr, &mCommandPool );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createSetupCommandBuffer(){
	if ( mSetupCommandBuffer != VK_NULL_HANDLE ){
		vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
		mSetupCommandBuffer = VK_NULL_HANDLE;
	}

	VkCommandBufferAllocateInfo info = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
		nullptr,                                        // const void*             pNext;
		mCommandPool,                                   // VkCommandPool           commandPool;
		VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
		1,                                              // uint32_t                commandBufferCount;
	};

 	// allocate one command buffer (as stated above) and store the handle to 
	// the newly allocated buffer into mSetupCommandBuffer
	vkAllocateCommandBuffers( mDevice, &info, &mSetupCommandBuffer );

	// todo : Command buffer is also started here, better put somewhere else
	// todo : Check if necessaray at all...
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	// todo : check null handles, flags?

	auto vkRes = vkBeginCommandBuffer( mSetupCommandBuffer, &cmdBufInfo );
	assert( !vkRes );
};

// ----------------------------------------------------------------------

void ofVkRenderer::setupSwapChain(){
	mSwapchain.setup( mInstance, mDevice, mPhysicalDevice, mWindowSurface, mWindowColorFormat, mSetupCommandBuffer, mWindowWidth, mWindowHeight );
};

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandBuffers(){
	VkCommandBufferAllocateInfo allocInfo;
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	allocInfo.pNext = VK_NULL_HANDLE;

	VkResult err = VK_SUCCESS;
	// Pre present
	err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPrePresentCommandBuffer );
	assert( !err);
	// Post present
	err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPostPresentCommandBuffer );
	assert( !err );
};

// ----------------------------------------------------------------------

bool  ofVkRenderer::getMemoryAllocationInfo( const VkMemoryRequirements& memReqs, VkFlags memProps, VkMemoryAllocateInfo& memInfo ) const
{
	memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memInfo.pNext = NULL;

	if ( !memReqs.size ){
		memInfo.allocationSize = 0;
		memInfo.memoryTypeIndex = ~0;
		return true;
	}

	// Find an available memory type that satifies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < mPhysicalDeviceMemoryProperties.memoryTypeCount; ++memoryTypeIndex ){
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
			( mPhysicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps ) == memProps ){
			break;
		}
	}
	if ( memoryTypeIndex >= mPhysicalDeviceMemoryProperties.memoryTypeCount ){
		assert( 0 && "memorytypeindex not found" );
		return false;
	}

	memInfo.allocationSize = memReqs.size;
	memInfo.memoryTypeIndex = memoryTypeIndex;

	return true;
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDepthStencil(){
	VkImageCreateInfo image = {};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.pNext = NULL;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = mDepthFormat;
	image.extent = { mWindowWidth, mWindowHeight, 1 };
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image.flags = 0;

	VkImageViewCreateInfo depthStencilView = {};
	depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilView.pNext = NULL;
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = mDepthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;

	VkMemoryRequirements memReqs;
	VkResult err;

	err = vkCreateImage( mDevice, &image, nullptr, &mDepthStencil.image );
	assert( !err );
	vkGetImageMemoryRequirements( mDevice, mDepthStencil.image, &memReqs );

	VkMemoryAllocateInfo memInfo;
	getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo );
	
	err = vkAllocateMemory( mDevice, &memInfo, nullptr, &mDepthStencil.mem );
	assert( !err );

	err = vkBindImageMemory( mDevice, mDepthStencil.image, mDepthStencil.mem, 0 );
	assert( !err );

	auto transferBarrier = of::vk::createImageBarrier(
		mDepthStencil.image,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 
	);

	// Append pipeline barrier to current setup commandBuffer
	vkCmdPipelineBarrier(
		mSetupCommandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &transferBarrier );

		/*VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlags                        srcStageMask,
		VkPipelineStageFlags                        dstStageMask,
		VkDependencyFlags                           dependencyFlags,
		uint32_t                                    memoryBarrierCount,
		const VkMemoryBarrier*                      pMemoryBarriers,
		uint32_t                                    bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
		uint32_t                                    imageMemoryBarrierCount,
		const VkImageMemoryBarrier*                 pImageMemoryBarriers*/


	depthStencilView.image = mDepthStencil.image;

	err = vkCreateImageView( mDevice, &depthStencilView, nullptr, &mDepthStencil.view );
	assert( !err );
};

// ----------------------------------------------------------------------

void ofVkRenderer::setupRenderPass(){
	
	VkAttachmentDescription attachments[2] = {
		{   // Color attachment
	    
			// Note that we keep initialLayout of this color attachment 
			// `VK_IMAGE_LAYOUT_UNDEFINED` -- we do this to say we effectively don't care
			// about the initial layout and contents of (swapchain) images which 
			// are attached here. See also: 
			// http://stackoverflow.com/questions/37524032/how-to-deal-with-the-layouts-of-presentable-images
			//
			// We might re-investigate this and pre-transfer images to COLOR_OPTIMAL, but only on initial use, 
			// if we wanted to be able to accumulate drawing into this buffer.
			
			0,                                                 // VkAttachmentDescriptionFlags    flags;
			mWindowColorFormat.format,                         // VkFormat                        format;
			VK_SAMPLE_COUNT_1_BIT,                             // VkSampleCountFlagBits           samples;
			VK_ATTACHMENT_LOAD_OP_CLEAR,                       // VkAttachmentLoadOp              loadOp;
			VK_ATTACHMENT_STORE_OP_STORE,                      // VkAttachmentStoreOp             storeOp;
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,                   // VkAttachmentLoadOp              stencilLoadOp;
			VK_ATTACHMENT_STORE_OP_DONT_CARE,                  // VkAttachmentStoreOp             stencilStoreOp;
			VK_IMAGE_LAYOUT_UNDEFINED,                         // VkImageLayout                   initialLayout;
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,          // VkImageLayout                   finalLayout; 
		},
		{   // Depth attachment
			0,                                                 // VkAttachmentDescriptionFlags    flags;
			mDepthFormat,                                      // VkFormat                        format;
			VK_SAMPLE_COUNT_1_BIT,                             // VkSampleCountFlagBits           samples;
			VK_ATTACHMENT_LOAD_OP_CLEAR,                       // VkAttachmentLoadOp              loadOp;
			VK_ATTACHMENT_STORE_OP_STORE,                      // VkAttachmentStoreOp             storeOp;
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,                   // VkAttachmentLoadOp              stencilLoadOp;
			VK_ATTACHMENT_STORE_OP_DONT_CARE,                  // VkAttachmentStoreOp             stencilStoreOp;
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,  // VkImageLayout                   initialLayout;
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,  // VkImageLayout                   finalLayout; 
		},
	};

	VkAttachmentReference colorReference = {
		0,                                                     // uint32_t         attachment;
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,              // VkImageLayout    layout;
	};

	VkAttachmentReference depthReference = {
		1,                                                     // uint32_t         attachment;
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,      // VkImageLayout    layout;
	};

	VkSubpassDescription subpass = {
		0,                                                     // VkSubpassDescriptionFlags       flags;
		VK_PIPELINE_BIND_POINT_GRAPHICS,                       // VkPipelineBindPoint             pipelineBindPoint;
		0,                                                     // uint32_t                        inputAttachmentCount;
		nullptr,                                               // const VkAttachmentReference*    pInputAttachments;
		1,                                                     // uint32_t                        colorAttachmentCount;
		&colorReference,                                       // const VkAttachmentReference*    pColorAttachments;
		nullptr,                                               // const VkAttachmentReference*    pResolveAttachments;
		&depthReference,                                       // const VkAttachmentReference*    pDepthStencilAttachment;
		0,                                                     // uint32_t                        preserveAttachmentCount;
		nullptr,                                               // const uint32_t*                 pPreserveAttachments;
	};

	VkRenderPassCreateInfo renderPassInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,             // VkStructureType                   sType;
		nullptr,                                               // const void*                       pNext;
		0,                                                     // VkRenderPassCreateFlags           flags;
		2,                                                     // uint32_t                          attachmentCount;
		attachments,                                           // const VkAttachmentDescription*    pAttachments;
		1,                                                     // uint32_t                          subpassCount;
		&subpass,                                              // const VkSubpassDescription*       pSubpasses;
		0,                                                     // uint32_t                          dependencyCount;
		nullptr,                                               // const VkSubpassDependency*        pDependencies;
	};

	VkResult err = vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass);
	assert(!err);
};


// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameBuffer(){

	// Create frame buffers for every swap chain frame
	mFrameBuffers.resize( mSwapchain.getImageCount() );
	for ( uint32_t i = 0; i < mFrameBuffers.size(); i++ ){
		// This is where we connect the framebuffer with the presentable image buffer
		// which is handled by the swapchain.
		// TODO: the swapchain should own this frame buffer, 
		// and allow us to reference it.
		// maybe this needs to move into the swapchain.
		
		VkImageView attachments[2];
		// attachment0 shall be the image view for the image buffer to the corresponding swapchain image view
		attachments[0] = mSwapchain.getImage(i).view;
		// attachment1 shall be the image view for the depthStencil buffer
		attachments[1] = mDepthStencil.view;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = mRenderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments;
		frameBufferCreateInfo.width = mWindowWidth;
		frameBufferCreateInfo.height = mWindowHeight;
		frameBufferCreateInfo.layers = 1;

		// create a framebuffer for each swap chain frame
		VkResult err = vkCreateFramebuffer( mDevice, &frameBufferCreateInfo, nullptr, &mFrameBuffers[i] );
		assert( !err );
	}
};

// ----------------------------------------------------------------------

void ofVkRenderer::flushSetupCommandBuffer(){
	VkResult err;

	if ( mSetupCommandBuffer == VK_NULL_HANDLE )
		return;

	err = vkEndCommandBuffer( mSetupCommandBuffer );
	assert( !err );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mSetupCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	err = vkQueueWaitIdle( mQueue );
	assert( !err );

	vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
	mSetupCommandBuffer = VK_NULL_HANDLE; // todo : check if still necessary
};

// ----------------------------------------------------------------------

void ofVkRenderer::beginDrawCommandBuffer(VkCommandBuffer& cmdBuf_){
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	// Set target frame buffer
	vkBeginCommandBuffer( cmdBuf_, &cmdBufInfo );

	// Update dynamic viewport state
	VkViewport viewport = {};
	viewport.width = (float)mViewport.width;
	viewport.height = (float)mViewport.height;
	viewport.minDepth = ( float ) 0.0f;		   // this is the min depth value for the depth buffer
	viewport.maxDepth = ( float ) 1.0f;		   // this is the max depth value for the depth buffer  
	vkCmdSetViewport( cmdBuf_, 0, 1, &viewport );

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = mWindowWidth;
	scissor.extent.height = mWindowHeight;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor( cmdBuf_, 0, 1, &scissor );

	beginRenderPass(cmdBuf_, mFrameBuffers[mSwapchain.getCurrentImageIndex()] );
}

// ----------------------------------------------------------------------

void ofVkRenderer::beginRenderPass(VkCommandBuffer& cmdBuf_, VkFramebuffer& frameBuf_){
	VkClearValue clearValues[2];
	clearValues[0].color = mDefaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRect2D renderArea{
		{ 0, 0 },								  // VkOffset2D
		{ mWindowWidth, mWindowHeight },		  // VkExtent2D
	};

	//auto currentFrameBufferId = mSwapchain.getCurrentBuffer();

	VkRenderPassBeginInfo renderPassBeginInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType        sType;
		nullptr,                                  // const void*            pNext;
		mRenderPass,                              // VkRenderPass           renderPass;
		frameBuf_,      // VkFramebuffer          framebuffer;
		renderArea,                               // VkRect2D               renderArea;
		2,                                        // uint32_t               clearValueCount;
		clearValues,                              // const VkClearValue*    pClearValues;
	};

	// VK_SUBPASS_CONTENTS_INLINE means we're putting all our render commands into
	// the primary command buffer - otherwise we would have to call execute on secondary
	// command buffers to draw.
	vkCmdBeginRenderPass( cmdBuf_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
};

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){
	
	// vkDeviceWaitIdle( mDevice );

	// start of new frame
	VkResult err;

	// + block cpu until swapchain can get next image, 
	// + get index for swapchain image we may render into,
	// + signal presentComplete once the image has been acquired
	uint32_t swapIdx;

	err = mSwapchain.acquireNextImage( mSemaphorePresentComplete, &swapIdx);
	assert( !err );

	// todo: transfer image from undefined to COLOR_ATTACHMENT_OPTIMAL 
	// when we're looking at the first use of this image.

	//auto transferBarrier = of::vk::createImageBarrier( mImages[i].imageRef,
	//	VK_IMAGE_ASPECT_COLOR_BIT,
	//	VK_IMAGE_LAYOUT_UNDEFINED,
	//	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

	//// Append pipeline barrier to commandBuffer
	//vkCmdPipelineBarrier(
	//	cmdBuffer,
	//	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	//	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	//	0,
	//	0, nullptr,
	//	0, nullptr,
	//	1, &transferBarrier );


	{
		if ( mDrawCmdBuffer.size() == mSwapchain.getImageCount() ){
			// if command buffer has been previously recorded, we want to re-use it.
			vkResetCommandBuffer( mDrawCmdBuffer[swapIdx], 0 );
		} else {
			// allocate a draw command buffer for each swapchain image
			mDrawCmdBuffer.resize( mSwapchain.getImageCount() );
			// (re)allocate command buffer used for draw commands
			VkCommandBufferAllocateInfo allocInfo = {
				VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,                 // VkStructureType         sType;
				nullptr,                                                        // const void*             pNext;
				mCommandPool,                                                   // VkCommandPool           commandPool;
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,                                // VkCommandBufferLevel    level;
				mDrawCmdBuffer.size()                                           // uint32_t                commandBufferCount;
			};
			
			vkAllocateCommandBuffers( mDevice, &allocInfo, mDrawCmdBuffer.data() );
		}
	}
	
	mContext->begin( swapIdx );
	beginDrawCommandBuffer(mDrawCmdBuffer[swapIdx]);

}

// ----------------------------------------------------------------------

void ofVkRenderer::endDrawCommandBuffer(){
	endRenderPass();
	vkEndCommandBuffer( mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()] );
}

// ----------------------------------------------------------------------

void ofVkRenderer::endRenderPass(){
	vkCmdEndRenderPass( mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()] );
};

// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){
	VkResult err;
	

	// submit current model view and projection matrices
	
	endDrawCommandBuffer();
	mContext->end();

	// Submit the draw command buffer
	//
	// The submit info structure contains a list of
	// command buffers and semaphores to be submitted to a queue
	// If you want to submit multiple command buffers, pass an array
	VkPipelineStageFlags pipelineStages[] = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
	
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO,                       // VkStructureType                sType;
		nullptr,                                             // const void*                    pNext;
		1,                                                   // uint32_t                       waitSemaphoreCount;
		&mSemaphorePresentComplete,                          // const VkSemaphore*             pWaitSemaphores;
		pipelineStages,                                      // const VkPipelineStageFlags*    pWaitDstStageMask;
		1,                                                   // uint32_t                       commandBufferCount;
		&mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()],  // const VkCommandBuffer*         pCommandBuffers;
		1,                                                   // uint32_t                       signalSemaphoreCount;
		&mSemaphoreRenderComplete,                           // const VkSemaphore*             pSignalSemaphores;
	};

	// Submit to the graphics queue	- 
	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	{  // pre-present

		/*
		
		We have to transfer the image layout of our current color attachment 
		from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		so that it can be handed over to the swapchain, ready for presenting. 
		
		The attachment arrives in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL because that's 
		how our main renderpass, mRenderPass, defines it in its finalLayout parameter.
		
		*/

		VkCommandBufferBeginInfo beginInfo = {
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,     // VkStructureType                          sType;
			nullptr,                                         // const void*                              pNext;
			0,                                               // VkCommandBufferUsageFlags                flags;
			nullptr,                                         // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
		};
		
		vkBeginCommandBuffer( mPrePresentCommandBuffer, &beginInfo );
		{
			auto transferBarrier = of::vk::createImageBarrier(	
				mSwapchain.getImage( mSwapchain.getCurrentImageIndex() ).imageRef,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

			// Append pipeline barrier to commandBuffer
			vkCmdPipelineBarrier(
				mPrePresentCommandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &transferBarrier );
		}
		vkEndCommandBuffer( mPrePresentCommandBuffer );
		// Submit to the queue
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mPrePresentCommandBuffer;

		err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	}

	// Present the current buffer to the swap chain
	// We pass the signal semaphore from the submit info
	// to ensure that the image is not rendered until
	// all commands have been submitted
	mSwapchain.queuePresent( mQueue, mSwapchain.getCurrentImageIndex(), { mSemaphoreRenderComplete } );
	
	// Add a post present image memory barrier
	// This will transform the frame buffer color attachment back
	// to it's initial layout after it has been presented to the
	// windowing system
	// See buildCommandBuffers for the pre present barrier that 
	// does the opposite transformation 
	VkImageMemoryBarrier postPresentBarrier = {};
	postPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	postPresentBarrier.pNext = NULL;
	postPresentBarrier.srcAccessMask = 0;
	postPresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	postPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	postPresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	postPresentBarrier.image = mSwapchain.getImage( mSwapchain.getCurrentImageIndex() ).imageRef;

	// Use dedicated command buffer from example base class for submitting the post present barrier
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	err = vkBeginCommandBuffer( mPostPresentCommandBuffer, &cmdBufInfo );

	// Put post present barrier into command buffer
	vkCmdPipelineBarrier(
		mPostPresentCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &postPresentBarrier );

	err = vkEndCommandBuffer( mPostPresentCommandBuffer );

	// Submit to the queue
	submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mPostPresentCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	err = vkQueueWaitIdle( mQueue );

}

// ----------------------------------------------------------------------

void ofVkRenderer::draw( const ofMesh & mesh_, ofPolyRenderMode renderType, bool useColors, bool useTextures, bool useNormals ) const{

	// store uniforms if needed

	std::vector<uint32_t> dynamicOffsets = { 
	    uint32_t(mContext->getCurrentMatrixStateOffset()),
	};

	auto & currentShader = mShaders[0];

	vector<VkDescriptorSet>  currentlyBoundDescriptorsets = {
		mDescriptorSets[0],                  // default matrix uniforms
		                                     // if there were any other uniforms bound
	};

	auto & cmd = mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()];

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		cmd,
	    VK_PIPELINE_BIND_POINT_GRAPHICS,                // use graphics, not compute pipeline
	    *mPipelineLayouts[0],                           // which pipeline layout (contains the bindings programmed from an sequence of descriptor sets )
	    0, 						                        // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
	    uint32_t(currentlyBoundDescriptorsets.size()),  // setCount: how many sets to bind
	    currentlyBoundDescriptorsets.data(),            // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
	    uint32_t(dynamicOffsets.size()),                // dynamic offsets count how many dynamic offsets
	    dynamicOffsets.data()                           // dynamic offsets for each
	);

	// Bind the rendering pipeline (including the shaders)
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelines.solid );

	std::vector<VkDeviceSize> vertexOffsets;
	std::vector<VkDeviceSize> indexOffsets;

	// Store vertex data using Context.
	// - this uses Allocator to store mesh data in the current frame' s dynamic memory
	// Context will return memory offsets into vertices, indices, based on current context memory buffer
	// 
	// TODO: check if it made sense to cache already stored meshes, 
	//       so that meshes which have already been stored this frame 
	//       may be re-used.
	mContext->storeMesh( mesh_, vertexOffsets, indexOffsets);

	// TODO: cull vertexOffsets which refer to empty vertex attribute data
	//       make sure that a pipeline with the correct bindings is bound to match the 
	//       presence or non-presence of mesh data.

	// Bind vertex data buffers to current pipeline. 
	// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
	// See Shader.h for an explanation of how this is mapped to shader attribute locations
	vector<VkBuffer> bufferRefs( vertexOffsets.size(), mContext->getVkBuffer() );
	vkCmdBindVertexBuffers( cmd, 0, uint32_t(bufferRefs.size()), bufferRefs.data(), vertexOffsets.data() );

	if ( indexOffsets.empty() ){
		// non-indexed draw
		vkCmdDraw( cmd, uint32_t(mesh_.getNumVertices()), 1, 0, 1 );
	} else{
		// indexed draw
		vkCmdBindIndexBuffer( cmd, bufferRefs[0], indexOffsets[0], VK_INDEX_TYPE_UINT32 );
		vkCmdDrawIndexed( cmd, uint32_t(mesh_.getNumIndices()), 1, 0, 0, 1 );
	}
}  

