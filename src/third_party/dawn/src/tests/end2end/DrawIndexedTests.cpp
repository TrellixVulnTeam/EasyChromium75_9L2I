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

#include "tests/DawnTest.h"

#include "utils/ComboRenderPipelineDescriptor.h"
#include "utils/DawnHelpers.h"

constexpr uint32_t kRTSize = 4;

class DrawIndexedTest : public DawnTest {
    protected:
        void SetUp() override {
            DawnTest::SetUp();

            renderPass = utils::CreateBasicRenderPass(device, kRTSize, kRTSize);

            dawn::ShaderModule vsModule =
                utils::CreateShaderModule(device, dawn::ShaderStage::Vertex, R"(
                #version 450
                layout(location = 0) in vec4 pos;
                void main() {
                    gl_Position = pos;
                })");

            dawn::ShaderModule fsModule = utils::CreateShaderModule(device, dawn::ShaderStage::Fragment, R"(
                #version 450
                layout(location = 0) out vec4 fragColor;
                void main() {
                    fragColor = vec4(0.0, 1.0, 0.0, 1.0);
                })"
            );

            utils::ComboRenderPipelineDescriptor descriptor(device);
            descriptor.cVertexStage.module = vsModule;
            descriptor.cFragmentStage.module = fsModule;
            descriptor.primitiveTopology = dawn::PrimitiveTopology::TriangleStrip;
            descriptor.cInputState.numInputs = 1;
            descriptor.cInputState.cInputs[0].stride = 4 * sizeof(float);
            descriptor.cInputState.numAttributes = 1;
            descriptor.cInputState.cAttributes[0].format = dawn::VertexFormat::Float4;
            descriptor.cColorStates[0]->format = renderPass.colorFormat;

            pipeline = device.CreateRenderPipeline(&descriptor);

            vertexBuffer = utils::CreateBufferFromData<float>(device, dawn::BufferUsageBit::Vertex, {
                // First quad: the first 3 vertices represent the bottom left triangle
                -1.0f, -1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 0.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f,

                 // Second quad: the first 3 vertices represent the top right triangle
                -1.0f, -1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f
            });
            indexBuffer = utils::CreateBufferFromData<uint32_t>(
                device, dawn::BufferUsageBit::Index,
                {0, 1, 2, 0, 3, 1,
                 // The indices below are added to test negatve baseVertex
                 0 + 4, 1 + 4, 2 + 4, 0 + 4, 3 + 4, 1 + 4});
        }

        utils::BasicRenderPass renderPass;
        dawn::RenderPipeline pipeline;
        dawn::Buffer vertexBuffer;
        dawn::Buffer indexBuffer;

        void Test(uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t baseVertex,
                  uint32_t firstInstance,
                  uint64_t bufferOffset,
                  RGBA8 bottomLeftExpected,
                  RGBA8 topRightExpected) {
            uint64_t zeroOffset = 0;
            dawn::CommandEncoder encoder = device.CreateCommandEncoder();
            {
                dawn::RenderPassEncoder pass = encoder.BeginRenderPass(
                    &renderPass.renderPassInfo);
                pass.SetPipeline(pipeline);
                pass.SetVertexBuffers(0, 1, &vertexBuffer, &zeroOffset);
                pass.SetIndexBuffer(indexBuffer, bufferOffset);
                pass.DrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
                pass.EndPass();
            }

            dawn::CommandBuffer commands = encoder.Finish();
            queue.Submit(1, &commands);

            EXPECT_PIXEL_RGBA8_EQ(bottomLeftExpected, renderPass.color, 1, 3);
            EXPECT_PIXEL_RGBA8_EQ(topRightExpected, renderPass.color, 3, 1);
        }
};

// The most basic DrawIndexed triangle draw.
TEST_P(DrawIndexedTest, Uint32) {

    RGBA8 filled(0, 255, 0, 255);
    RGBA8 notFilled(0, 0, 0, 0);

    // Test a draw with no indices.
    Test(0, 0, 0, 0, 0, 0, notFilled, notFilled);
    // Test a draw with only the first 3 indices of the first quad (bottom left triangle)
    Test(3, 1, 0, 0, 0, 0, filled, notFilled);
    // Test a draw with only the last 3 indices of the first quad (top right triangle)
    Test(3, 1, 3, 0, 0, 0, notFilled, filled);
    // Test a draw with all 6 indices (both triangles).
    Test(6, 1, 0, 0, 0, 0, filled, filled);
}

// Test the parameter 'baseVertex' of DrawIndexed() works.
TEST_P(DrawIndexedTest, BaseVertex) {
    RGBA8 filled(0, 255, 0, 255);
    RGBA8 notFilled(0, 0, 0, 0);

    // Test a draw with only the first 3 indices of the second quad (top right triangle)
    Test(3, 1, 0, 4, 0, 0, notFilled, filled);
    // Test a draw with only the last 3 indices of the second quad (bottom left triangle)
    Test(3, 1, 3, 4, 0, 0, filled, notFilled);

    // Test negative baseVertex
    // Test a draw with only the first 3 indices of the first quad (bottom left triangle)
    Test(3, 1, 0, -4, 0, 6 * sizeof(uint32_t), filled, notFilled);
    // Test a draw with only the last 3 indices of the first quad (top right triangle)
    Test(3, 1, 3, -4, 0, 6 * sizeof(uint32_t), notFilled, filled);
}

DAWN_INSTANTIATE_TEST(DrawIndexedTest, D3D12Backend, MetalBackend, OpenGLBackend, VulkanBackend);
