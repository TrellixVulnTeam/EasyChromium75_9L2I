// Copyright 2019 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VK_FORMAT_UTILS_HPP_
#define VK_FORMAT_UTILS_HPP_

#include <vulkan/vulkan_core.h>

namespace sw
{
	struct float4;
}

namespace vk
{

class Format
{
public:
	Format() {}
	Format(VkFormat format) : format(format) {}
	inline operator VkFormat() const { return format; }

	bool isSignedNonNormalizedInteger() const;
	bool isUnsignedNonNormalizedInteger() const;
	bool isNonNormalizedInteger() const;

	bool isStencil() const;
	bool isDepth() const;
	bool hasQuadLayout() const;

	bool isSRGBformat() const;
	bool isSRGBreadable() const;
	bool isSRGBwritable() const;
	bool isFloatFormat() const;

	bool isCompatible(const Format& other) const;
	bool isCompressed() const;
	int blockWidth() const;
	int blockHeight() const;
	int bytesPerBlock() const;

	int componentCount() const;
	bool isUnsignedComponent(int component) const;

	int bytes() const;
	int pitchB(int width, int border, bool target) const;
	int sliceB(int width, int height, int border, bool target) const;

	bool getScale(sw::float4 &scale) const;

	// Texture sampling utilities
	bool has16bitTextureFormat() const;
	bool has8bitTextureComponents() const;
	bool has16bitTextureComponents() const;
	bool has32bitIntegerTextureComponents() const;
	bool hasYuvFormat() const;
	bool isRGBComponent(int component) const;

private:
	VkFormat compatibleFormat() const;

	VkFormat format = VK_FORMAT_UNDEFINED;
};

} // namespace vk

#endif // VK_FORMAT_UTILS_HPP_