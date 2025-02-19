// Copyright 2018 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "utils/ComboRenderPipelineDescriptor.h"

#include "utils/DawnHelpers.h"

namespace utils {

    ComboInputStateDescriptor::ComboInputStateDescriptor() {
        dawn::InputStateDescriptor* descriptor = this;

        descriptor->indexFormat = dawn::IndexFormat::Uint32;

        // Fill the default values for vertexInput.
        descriptor->numInputs = 0;
        dawn::VertexInputDescriptor vertexInput;
        vertexInput.inputSlot = 0;
        vertexInput.stride = 0;
        vertexInput.stepMode = dawn::InputStepMode::Vertex;
        for (uint32_t i = 0; i < kMaxVertexInputs; ++i) {
            cInputs[i] = vertexInput;
        }
        descriptor->inputs = &cInputs[0];

        // Fill the default values for vertexAttribute.
        descriptor->numAttributes = 0;
        dawn::VertexAttributeDescriptor vertexAttribute;
        vertexAttribute.shaderLocation = 0;
        vertexAttribute.inputSlot = 0;
        vertexAttribute.offset = 0;
        vertexAttribute.format = dawn::VertexFormat::Float;
        for (uint32_t i = 0; i < kMaxVertexAttributes; ++i) {
            cAttributes[i] = vertexAttribute;
        }
        descriptor->attributes = &cAttributes[0];
    }

    ComboRenderPipelineDescriptor::ComboRenderPipelineDescriptor(const dawn::Device& device) {
        dawn::RenderPipelineDescriptor* descriptor = this;

        descriptor->primitiveTopology = dawn::PrimitiveTopology::TriangleList;
        descriptor->sampleCount = 1;

        // Set defaults for the vertex stage descriptor.
        {
            descriptor->vertexStage = &cVertexStage;
            cVertexStage.entryPoint = "main";
        }

        // Set defaults for the fragment stage desriptor.
        {
            descriptor->fragmentStage = &cFragmentStage;
            cFragmentStage.entryPoint = "main";
        }

        // Set defaults for the input state descriptors.
        descriptor->inputState = &cInputState;

        // Set defaults for the rasterization state descriptor.
        {
            cRasterizationState.frontFace = dawn::FrontFace::CCW;
            cRasterizationState.cullMode = dawn::CullMode::None;

            cRasterizationState.depthBias = 0;
            cRasterizationState.depthBiasSlopeScale = 0.0;
            cRasterizationState.depthBiasClamp = 0.0;
            descriptor->rasterizationState = &cRasterizationState;
        }

        // Set defaults for the color state descriptors.
        {
            descriptor->colorStateCount = 1;
            descriptor->colorStates = &cColorStates[0];

            dawn::BlendDescriptor blend;
            blend.operation = dawn::BlendOperation::Add;
            blend.srcFactor = dawn::BlendFactor::One;
            blend.dstFactor = dawn::BlendFactor::Zero;
            dawn::ColorStateDescriptor colorStateDescriptor;
            colorStateDescriptor.format = dawn::TextureFormat::R8G8B8A8Unorm;
            colorStateDescriptor.alphaBlend = blend;
            colorStateDescriptor.colorBlend = blend;
            colorStateDescriptor.writeMask = dawn::ColorWriteMask::All;
            for (uint32_t i = 0; i < kMaxColorAttachments; ++i) {
                mColorStates[i] = colorStateDescriptor;
                cColorStates[i] = &mColorStates[i];
            }
        }

        // Set defaults for the depth stencil state descriptors.
        {
            dawn::StencilStateFaceDescriptor stencilFace;
            stencilFace.compare = dawn::CompareFunction::Always;
            stencilFace.failOp = dawn::StencilOperation::Keep;
            stencilFace.depthFailOp = dawn::StencilOperation::Keep;
            stencilFace.passOp = dawn::StencilOperation::Keep;

            cDepthStencilState.format = dawn::TextureFormat::D32FloatS8Uint;
            cDepthStencilState.depthWriteEnabled = false;
            cDepthStencilState.depthCompare = dawn::CompareFunction::Always;
            cDepthStencilState.stencilBack = stencilFace;
            cDepthStencilState.stencilFront = stencilFace;
            cDepthStencilState.stencilReadMask = 0xff;
            cDepthStencilState.stencilWriteMask = 0xff;
            descriptor->depthStencilState = nullptr;
        }

        descriptor->layout = utils::MakeBasicPipelineLayout(device, nullptr);
    }

}  // namespace utils
