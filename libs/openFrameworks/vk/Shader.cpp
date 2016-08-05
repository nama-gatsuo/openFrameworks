#include "vk/Shader.h"
#include "vk/Context.h"
#include "ofLog.h"
#include "ofFileUtils.h"
#include "spooky/SpookyV2.h"
#include "shaderc/shaderc.hpp"

// ----------------------------------------------------------------------

of::vk::Shader::Shader( const Settings & settings_ )
	: mSettings( settings_ )
	, mContext(settings_.context)
	, mDevice( settings_.context->mSettings.device)
{
	setup();
}

// ----------------------------------------------------------------------

void of::vk::Shader::setup(){
	bool shaderDirty = false;

	for ( auto & source : mSettings.sources ){

		const auto & shaderType = source.first;
		const auto & filename = source.second;

		if ( !ofFile( filename ).exists() ){
			ofLogError() << "Shader file not found: " << source.second;
			continue;
		}

		std::vector<uint32_t> spirCode;
		getSpirV( shaderType, filename, spirCode );	/* load or compiles code into spirCode */

		bool spirCodeDirty = isSpirCodeDirty( shaderType, spirCode );

		if ( spirCodeDirty ){
			createVkShaderModule( shaderType, filename, std::move( spirCode ) );
			// ! TODO: this also means that all derived pipelines need to be re-created.
		}

		shaderDirty |= spirCodeDirty;
	}

	if ( shaderDirty ){
		reflect();
		buildSetLayouts();
	}
}

// ----------------------------------------------------------------------

bool of::vk::Shader::isSpirCodeDirty( const VkShaderStageFlagBits shaderStage, std::vector<uint32_t> &spirCode_ ){

	uint64_t spirvHash = SpookyHash::Hash64(reinterpret_cast<char*>( spirCode_.data() ), spirCode_.size() * sizeof( uint32_t ),	0 );

	if ( mSpvHash.find( shaderStage ) == mSpvHash.end() ){
		// hash not found so must be dirty
		mSpvHash[shaderStage] = spirvHash;
		return true;
	} else{
		return ( mSpvHash[shaderStage] != spirvHash );
	}
	
	return false;
}

// ----------------------------------------------------------------------

void of::vk::Shader::getSpirV( const VkShaderStageFlagBits shaderStage, const std::string & fileName, std::vector<uint32_t> &spirCode ){
	
	auto f = ofFile( fileName );
	auto fExt = f.getExtension();

	if ( fExt == "spv" ){
		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		ofLogNotice() << "Loading SPIR-V shader module: " << fileName;
		auto a = fileBuf.getData();
		spirCode.assign(
			reinterpret_cast<uint32_t*>( fileBuf.getData() ),
			reinterpret_cast<uint32_t*>( fileBuf.getData() ) + fileBuf.size() / sizeof( uint32_t )
		);
	} else {
		shaderc_shader_kind shaderType = shaderc_shader_kind::shaderc_glsl_infer_from_source;

		switch ( shaderStage ){
		case VK_SHADER_STAGE_VERTEX_BIT:
			shaderType = shaderc_shader_kind::shaderc_glsl_default_vertex_shader;
			break;
		case VK_SHADER_STAGE_FRAGMENT_BIT:
			shaderType = shaderc_shader_kind::shaderc_glsl_default_fragment_shader;
			break;
		default:
			break;
		}

		bool success = true;
		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;

		// Like -DMY_DEFINE=1
		// options.AddMacroDefinition( "MY_DEFINE", "1" );

		ofLogNotice() << "Compiling GLSL shader module: " << fileName;

		shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
			fileBuf.getData(), fileBuf.size(), shaderType, fileName.c_str(), options );

		if ( module.GetCompilationStatus() != shaderc_compilation_status_success ){
			ofLogError() << "ERR\tShader compile: " << module.GetErrorMessage();
		
		} else{
			spirCode.clear();
			spirCode.assign( module.cbegin(), module.cend() );
			ofLogNotice() << "OK \tShader compile: " << fileName;
		}
		
		assert( success );
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkShaderModule( const VkShaderStageFlagBits shaderType, const std::string & fileName, std::vector<uint32_t> &&spirCode ){

	VkShaderModuleCreateInfo info{
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,	            // VkStructureType              sType;
		nullptr,                                                    // const void*                  pNext;
		0,	                                                        // VkShaderModuleCreateFlags    flags;
		spirCode.size() * sizeof( uint32_t ),                       // size_t                       codeSize;
		spirCode.data()                                             // const uint32_t*              pCode;
	};

	VkShaderModule module;
	auto err = vkCreateShaderModule( mDevice, &info, nullptr, &module );

	if ( err == VK_SUCCESS ){

		mModules[shaderType] = module;

		VkPipelineShaderStageCreateInfo shaderStage{
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,    // VkStructureType                     sType;
			nullptr,                                                // const void*                         pNext;
			0,	                                                    // VkPipelineShaderStageCreateFlags    flags;
			shaderType,                                             // VkShaderStageFlagBits               stage;
			module,                                                 // VkShaderModule                      module;
			"main",                                                 // const char*                         pName;
			nullptr                                                 // const VkSpecializationInfo*         pSpecializationInfo;
		};

		mStages.push_back( std::move( shaderStage ) );

		// move the ir code buffer into the shader compiler
		mSpvCrossCompilers[shaderType] = make_shared<spirv_cross::Compiler>( std::move( spirCode ) );

	} else{
		ofLog() << "Error creating shader module: " << fileName;
	}
}


// ----------------------------------------------------------------------

void of::vk::Shader::reflect()
{

	// for all shader stages
	for ( auto &c : mSpvCrossCompilers ){

		auto & compiler = *c.second;
		auto & shaderStage = c.first;

		if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
			ofLog() << std::endl << std::endl << "Vertex Stage" << endl << string( 70, '-' );
		} else if ( shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT ){
			ofLog() << std::endl << std::endl << "Fragment Stage" << endl << string( 70, '-' );
		}

		auto shaderResources = compiler.get_shader_resources();

		// ! TODO: process texture samplers
		// This: http://gpuopen.com/wp-content/uploads/2016/03/VulkanFastPaths.pdf
		// suggests one fast path is to bind all (!) textures into ONE DescriptorSet / binding 
		// as an array of textures, and then use pushConstants to fetch the index 
		// into the array for the texture we want for this particular draw. 
		// This would mean to create one descriptor per texture and to bind all these 
		// texture descriptors to one binding - and to one descriptorset.
		for ( auto & sampled_image : shaderResources.sampled_images ){

		}

		// --- uniform buffers ---
		for ( auto & resource : shaderResources.uniform_buffers ){
			processResource( compiler, resource, shaderStage );
		} // end for : shaderResources.uniform_buffers

		// --- vertex inputs ---
		if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
			processVertexInputs( shaderResources, compiler );
		} 
	}  
}
// ----------------------------------------------------------------------

void of::vk::Shader::buildSetLayouts(){
	
	// group BindingInfo by "set"
	std::map<uint32_t, vector<BindingInfo>> bindingInfoMap;
	for ( auto & binding : mUniforms ){
		bindingInfoMap[binding.second.set].push_back( binding.second );
	};

	// go over all sets, and sort uniforms by binding number asc.
	for ( auto & s : bindingInfoMap ){
		auto & uniformInfoVec = s.second;
		std::sort( uniformInfoVec.begin(), uniformInfoVec.end(), []( const BindingInfo& lhs, const BindingInfo& rhs )->bool{
			return lhs.binding.binding < rhs.binding.binding;
		} );
	}

	// now we can create setLayouts 


	uint32_t i = 0;
	for ( auto & descriptorSet : bindingInfoMap ){
		
		const auto & setNumber   = descriptorSet.first;

		if ( setNumber != i ){
			// Q: is this really the case? it could be possible that shaders define sets they are not using. 
			//    and these sets would not require memory to be bound.
			ofLogError() << "DescriptorSet ids in shader cannot be sparse. Missing definition for descriptorSet: " << i;
		}

		SetLayout layout;
		layout.bindings = descriptorSet.second;

		layout.calculateHash();

		// calculate hash key for current set

		// Context checks whether a layout with the current signature already exists.
		// If yes, it will derive a shared pointer to the layout with this signature.
		// If no,  it will create a new DescriptorSetLayout, store it in Context, and return
		// a shared pointer to it.
		
		mDescriptorSetLayoutKeys.push_back( layout.key );
		mContext->storeDescriptorSetLayout( std::move(layout) );

		++i;
	}

}

// ----------------------------------------------------------------------

void of::vk::Shader::processResource( spirv_cross::Compiler & compiler, spirv_cross::Resource & ubo, const VkShaderStageFlagBits & shaderStage ){
	
	// we need to build a unique list of uniforms
	// and make sure that uniforms with the same name 
	// refer to the same binding number and set index.
	//
	// also if a uniform is referred to by more than one
	// shader stages this needs to be updated in the uniform's 
	// accessibility stage flags.

	ostringstream os;

	uint32_t descriptor_set = 0;
	uint32_t bindingNumber = 0;

	// get a bitmask representing uniform decorations 
	uint64_t decorationMask = compiler.get_decoration_mask( ubo.id );

	// get the storage type for this ubo
	auto storageType = compiler.get_type( ubo.type_id );
	// get the storageSize (in bytes) for this ubo
	uint32_t storageSize = compiler.get_declared_struct_size( storageType );

	if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
		descriptor_set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
		os << ", set = " << descriptor_set;
	} else{
		ofLogWarning() << "Warning: Shader uniform " << ubo.name << "does not specify set id, and will " << endl
			<< "therefore be mapped to set 0 - this might have unintended consequences.";
		// If undefined, set descriptor set id to 0. This is conformant with:
		// https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
		descriptor_set = 0;
	}

	if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
		bindingNumber = compiler.get_decoration( ubo.id, spv::DecorationBinding );
		os << ", binding = " << bindingNumber;
	} else{
		ofLogWarning() << "Shader uniform" << ubo.name << "does not specify binding number.";
	}

	ofLog() << "Uniform Block: '" << ubo.name << "'" << os.str();

	auto type = compiler.get_type( ubo.type_id );

	// type for ubo descriptors is struct
	// such structs will have member types, that is, they have elements within.
	for ( uint32_t tI = 0; tI != type.member_types.size(); ++tI ){
		auto mn = compiler.get_member_name( ubo.type_id, tI );
		ofLog() << "\\-" << "[" << tI << "] : " << mn;
	}

	
	// let's look up if the current block name already exists in the 
	// table of bindings for this shader, and if necessary update
	// the shader stage flags to permit access to all stages that need it:

	// shaderStage defines from which shader stages this layout is accessible
	VkShaderStageFlags layoutAccessibleFromStages = shaderStage;

	VkDescriptorSetLayoutBinding  newBinding{
		bindingNumber,                                        // uint32_t              binding;
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,            // VkDescriptorType      descriptorType;
															  // Note that descriptorCount will always be 1 with UNIFORM_BUFFER_DYNAMIC, as 
															  // arrays of UBOs are not allowed:
		1,                                                    // uint32_t              descriptorCount;
		layoutAccessibleFromStages,                           // VkShaderStageFlags    stageFlags;
		nullptr,                                              // const VkSampler*      pImmutableSamplers;
	};

	if ( mUniforms.find( ubo.name ) != mUniforms.end() ){
		// we have found a binding with the same name in another shader stage.
		// therefore we: 
		// 1.) need to update the binding accessiblity flag
		// 2.) do some error checking to make sure the binding is the same.

		auto& existingBinding = mUniforms[ubo.name];
		if ( existingBinding.set != descriptor_set
			|| existingBinding.binding.binding != bindingNumber ){
			ofLogError() << "Incompatible bindings between shader stages: " << ubo.name;
		} else{
			// all good, make sure the binding is also accessible in the 
			// current stage.
			newBinding.stageFlags |= existingBinding.binding.stageFlags;
		}
	}

	mUniforms[ubo.name].set = descriptor_set;
	mUniforms[ubo.name].binding = newBinding;
	mUniforms[ubo.name].size = storageSize;
	mUniforms[ubo.name].name = ubo.name;

	// add name, offsets and sizes for individual members inside this ubo binding.

	// get offset and range for elements from buffer
	auto bufferRanges = compiler.get_active_buffer_ranges( ubo.id );

	for ( const auto &r : bufferRanges ){
		auto memberName = compiler.get_member_name( ubo.type_id, r.index );
		mUniforms[ubo.name].memberRanges[memberName] = { r.offset, r.range };
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::processVertexInputs( spirv_cross::ShaderResources &shaderResources, spirv_cross::Compiler & compiler ){
	ofLog() << "Vertex Attribute locations";

	mVertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
	mVertexInfo.bindingDescription.resize( shaderResources.stage_inputs.size() );

	for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){

		auto & attributeInput = shaderResources.stage_inputs[i];
		auto attributeType = compiler.get_type( attributeInput.type_id );

		uint32_t location = i; // shader location qualifier mapped to binding number

		if ( ( 1ull << spv::DecorationLocation ) & compiler.get_decoration_mask( attributeInput.id ) ){
			location = compiler.get_decoration( attributeInput.id, spv::DecorationLocation );
		}

		ofLog() << "Vertex Attribute loc=[" << location << "] : " << attributeInput.name;

		// Binding Description: Describe how to read data from buffer based on binding number
		mVertexInfo.bindingDescription[i].binding = location;  // which binding number we are describing
		mVertexInfo.bindingDescription[i].stride = ( attributeType.width / 8 ) * attributeType.vecsize * attributeType.columns;
		mVertexInfo.bindingDescription[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		// Attribute description: Map shader location to pipeline binding number
		mVertexInfo.attribute[i].location = location;   // .location == which shader attribute location
		mVertexInfo.attribute[i].binding = location;    // .binding  == pipeline binding number == where attribute takes data from

		switch ( attributeType.vecsize ){
		case 3:
			mVertexInfo.attribute[i].format = VK_FORMAT_R32G32B32_SFLOAT;	     // 3-part float
			break;
		case 4:
			mVertexInfo.attribute[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;	 // 4-part float
			break;
		default:
			break;
		}
	}

	VkPipelineVertexInputStateCreateInfo vi{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
		nullptr,                                                   // const void*                                 pNext;
		0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
		uint32_t( mVertexInfo.bindingDescription.size() ),         // uint32_t                                    vertexBindingDescriptionCount;
		mVertexInfo.bindingDescription.data(),                     // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
		uint32_t( mVertexInfo.attribute.size() ),                  // uint32_t                                    vertexAttributeDescriptionCount;
		mVertexInfo.attribute.data()                               // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
	};

	mVertexInfo.vi = std::move( vi );
}



// ----------------------------------------------------------------------

void of::vk::Shader::SetLayout::calculateHash(){
	// calculate hash key based on current contents
	// of set, binding information, and size 
	
	// first, we have to convert the binding info
	// to plain old data, otherwise the hash will 
	// take into account the std::map for memberRanges, 
	// and this would make the hash non-deterministic.

	struct BindingInfoPOD
	{
		uint32_t set;
		VkDescriptorSetLayoutBinding binding;
		uint32_t size;
	};

	auto alignment = alignof( BindingInfoPOD );
	assert( alignment == 8 );

	std::vector<BindingInfoPOD> podBindingInfo;
	podBindingInfo.reserve( bindings.size() );

	for ( const auto& b : bindings ){
		podBindingInfo.push_back( { b.set, b.binding, b.size } );
	}

	void * baseAddr = (void*)podBindingInfo.data();
	
	// We can calculate the size and be pretty sure that there will be no random
	// padding data caught in the vector, as sizeof(BindingInfoPOD) is 40, which 
	// nicely aligns to 8 byte.
	
	auto msgSize = podBindingInfo.size() * sizeof( BindingInfoPOD );

	this->key = SpookyHash::Hash64(baseAddr , msgSize, 0 );
}

