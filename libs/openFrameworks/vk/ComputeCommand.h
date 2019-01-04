#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/HelperTypes.h"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "vk/Texture.h"
#include "vk/Context.h"
#include <glm/glm.hpp>

namespace of{
namespace vk{

class BufferAllocator;	   // ffdecl.
class Context;       // ffdecl.
class ComputeCommand
{

private:

	/* compute pipeline state is essentially just a link to the shader */
	ComputePipelineState mPipelineState; 

private:      /* transient data */

	uint64_t mPipelineHash = 0;

	// Bindings data for descriptorSets, (vector index == set number) - retrieved from shader on setup
	std::vector<DescriptorSetData_t>   mDescriptorSetData;

	// Lookup table for uniform name-> desciptorSetData - retrieved from shader on setup
	std::map<std::string, UniformId_t> mUniformDictionary;

	// set data for upload to ubo - data is stored locally 
	// until command is submitted
	void commitUniforms( BufferAllocator& alloc_ );

public:

	void setup( const ComputePipelineState& pipelineState );

	const ComputePipelineState&         getPipelineState() const;

	const DescriptorSetData_t&           getDescriptorSetData( size_t setId_ ) const;

	// store uniform values to staging cpu memory
	template <typename T>
	ComputeCommand & setUniform( const std::string& uniformName, const T& uniformValue_ );

	ComputeCommand & setTexture( const std::string& name, const of::vk::Texture& tex_ );
	ComputeCommand & setStorageBuffer( const std::string& name, const of::vk::BufferRegion& buf_ );

	void submit( of::vk::Context& rc_, const glm::uvec3& dims );

};

// ------------------------------------------------------------
// Inline getters and setters

template<typename T>
inline ComputeCommand& ComputeCommand::setUniform( const std::string & uniformName, const T & uniformValue_ ){

	auto uniformInfoIt = mUniformDictionary.find( uniformName );

	if ( uniformInfoIt == mUniformDictionary.end() ){
		ofLogWarning() << "Could not set Uniform '" << uniformName << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found

	const auto & uniformInfo = uniformInfoIt->second;

	if ( uniformInfo.dataRange < sizeof( T ) ){
		ofLogWarning() << "Could not set uniform '" << uniformName << "': Uniform data size does not match: "
			<< " Expected: " << uniformInfo.dataRange << ", received: " << sizeof( T ) << ".";
		return *this;
	}

	// --------| invariant: size match, we can copy data into our vector.

	auto & dataVec = mDescriptorSetData[uniformInfo.setIndex].dynamicUboData[uniformInfo.auxDataIndex];

	if ( uniformInfo.dataOffset + uniformInfo.dataRange <= dataVec.size() ){
		memcpy( dataVec.data() + uniformInfo.dataOffset, &uniformValue_, uniformInfo.dataRange );
	} else{
		ofLogError() << "Not enough space in local uniform storage. Has this drawCommand been properly initialised?";
	}

	return *this;
}

// ------------------------------------------------------------

inline ComputeCommand & ComputeCommand::setTexture( const std::string & name_, const Texture & tex_ ){

	auto uniformInfoIt = mUniformDictionary.find( name_ );

	if ( uniformInfoIt == mUniformDictionary.end() ){
		ofLogWarning() << "Could not set Texture '" << name_ << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found

	const auto & uniformInfo = uniformInfoIt->second;

	auto & imageAttachment = mDescriptorSetData[uniformInfo.setIndex].imageAttachment[uniformInfo.auxDataIndex];

	imageAttachment.sampler     = tex_.getSampler();
	imageAttachment.imageView   = tex_.getImageView();
	imageAttachment.imageLayout = tex_.getImageLayout();
 
	return *this;
}

// ------------------------------------------------------------

inline ComputeCommand & ComputeCommand::setStorageBuffer( const std::string & uniformName, const of::vk::BufferRegion& buf_ ){

	auto uniformInfoIt = mUniformDictionary.find( uniformName );

	if ( uniformInfoIt == mUniformDictionary.end() ){
		ofLogWarning() << "Could not set Storage Buffer '" << uniformName << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found

	const auto & uniformInfo = uniformInfoIt->second;

	auto & bufferAttachment = mDescriptorSetData[uniformInfo.setIndex].bufferAttachment[uniformInfo.auxDataIndex];

	bufferAttachment = buf_;

	return *this;
}

// ------------------------------------------------------------

inline const ComputePipelineState & ComputeCommand::getPipelineState() const{
	return mPipelineState;
}

// ------------------------------------------------------------

inline const DescriptorSetData_t & ComputeCommand::getDescriptorSetData( size_t setId_ ) const{
	return mDescriptorSetData[setId_];
}

// ------------------------------------------------------------

} // namespace vk
} // namespace of


