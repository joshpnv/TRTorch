#include "core/util/prelude.h"
#include "core/conversion/converters/converters.h"

namespace trtorch {
namespace core {
namespace conversion {
namespace converters {
namespace impl {
namespace {

bool MaxPoolingConverter(ConversionCtx* ctx, const torch::jit::Node* n, args& args) {
    auto in = args[0].ITensor();
    auto shape = util::toVec(in->getDimensions());

    // Max Pool needs at least 4D input
    if (shape.size() < 4) {
        auto new_shape = util::toDimsPad(shape, 4);
        LOG_DEBUG("Input shape is less than 4D got: " << util::toDims(shape) << ", inserting shuffle layer to reshape to 4D tensor shape: " << new_shape);
        auto shuffle = ctx->net->addShuffle(*in);
        shuffle->setReshapeDimensions(new_shape);
        shuffle->setName((util::node_info(n) + " [Reshape to " + util::toStr(new_shape) + ']').c_str());
        in = shuffle->getOutput(0);
    }


    auto kernel_size = util::toDims(args[1].unwrapToIntList());
    LOG_DEBUG("kernel_size: " << kernel_size);
    auto padding = util::toDims(args[3].unwrapToIntList());
    LOG_DEBUG("padding: " << padding);
    auto stride = util::toDims(args[2].unwrapToIntList());
    LOG_DEBUG("stride: " << stride);

    auto dilation = util::toDims(args[4].unwrapToIntList());

    TRTORCH_ASSERT(dilation == util::toDims(std::vector<int64_t>(dilation.nbDims, 1)), "Pooling dilation is not supported in TensorRT");

    LOG_DEBUG("dilation: " << dilation);
    LOG_WARNING("Dilation not used in max pooling converter");
    bool ceil_mode = args[5].unwrapToBool();

    auto new_layer = ctx->net->addPoolingNd(*in, nvinfer1::PoolingType::kMAX, kernel_size);
    TRTORCH_CHECK(new_layer, "Unable to create Max Pooling layer from node: " << *n);

    new_layer->setName(util::node_info(n).c_str());
    new_layer->setPaddingNd(padding);
    if (stride.nbDims != 2 && ctx->settings.device == nvinfer1::DeviceType::kDLA) {
        if (!ctx->settings.allow_gpu_fallback) {
            TRTORCH_THROW_ERROR("DLA Pooling stride is limited to 2D, allow GPU fallback");
        } else {
            LOG_WARNING("DLA Pooling stride is limited to 2D, will run on GPU");
        }
    }
    new_layer->setStrideNd(stride);

    auto padding_mode = ceil_mode ? nvinfer1::PaddingMode::kEXPLICIT_ROUND_UP :  nvinfer1::PaddingMode::kEXPLICIT_ROUND_DOWN;
    new_layer->setPaddingMode(padding_mode);

    new_layer->setName(util::node_info(n).c_str());
    auto out_tensor = ctx->AssociateValueAndTensor(n->outputs()[0], new_layer->getOutput(0));

    LOG_DEBUG("Output tensor shape: " << out_tensor->getDimensions());
    return true;
}

bool AvgPoolingConverter(ConversionCtx* ctx, const torch::jit::Node* n, args& args) {
    auto in = args[0].ITensor();
    auto shape = util::toVec(in->getDimensions());

    // Avg Pool needs at least 4D input
    if (shape.size() < 4) {
        auto new_shape = util::toDimsPad(shape, 4);
        LOG_DEBUG("Input shape is less than 4D got: " << util::toDims(shape) << ", inserting shuffle layer to reshape to 4D tensor shape: " << new_shape);
        auto shuffle = ctx->net->addShuffle(*in);
        shuffle->setReshapeDimensions(new_shape);
        shuffle->setName((util::node_info(n) + " [Reshape to " + util::toStr(new_shape) + ']').c_str());
        in = shuffle->getOutput(0);
    }


    auto kernel_size = util::toDims(args[1].unwrapToIntList());
    LOG_DEBUG("kernel_size: " << kernel_size);
    auto padding = util::toDims(args[3].unwrapToIntList());
    LOG_DEBUG("padding: " << padding);
    auto stride = util::toDims(args[2].unwrapToIntList());
    LOG_DEBUG("stride: " << stride);

    bool ceil_mode = args[4].unwrapToBool();
    bool count_inlcude_pad = args[5].unwrapToBool();

    auto new_layer = ctx->net->addPoolingNd(*in, nvinfer1::PoolingType::kAVERAGE, kernel_size);
    TRTORCH_CHECK(new_layer, "Unable to create Avg Pooling layer from node: " << *n);

    new_layer->setName(util::node_info(n).c_str());
    new_layer->setPaddingNd(padding);
    if (stride.nbDims != 2 && ctx->settings.device == nvinfer1::DeviceType::kDLA) {
        if (!ctx->settings.allow_gpu_fallback) {
            TRTORCH_THROW_ERROR("DLA Pooling stride is limited to 2D, allow GPU fallback");
        } else {
            LOG_WARNING("DLA Pooling stride is limited to 2D, will run on GPU");
        }
    }
    new_layer->setStrideNd(stride);

    auto padding_mode = ceil_mode ? nvinfer1::PaddingMode::kEXPLICIT_ROUND_UP :  nvinfer1::PaddingMode::kEXPLICIT_ROUND_DOWN;
    new_layer->setPaddingMode(padding_mode);
    new_layer->setAverageCountExcludesPadding(!count_inlcude_pad);

    if (!(args[6].IValue()->isNone())) {
        LOG_WARNING("Divisor override is now handled by Avg Pooling Converter");
    }

    new_layer->setName(util::node_info(n).c_str());
    auto out_tensor = ctx->AssociateValueAndTensor(n->outputs()[0], new_layer->getOutput(0));

    LOG_DEBUG("Output tensor shape: " << out_tensor->getDimensions());
    return true;
}

auto pooling_registrations TRTORCH_UNUSED = RegisterNodeConversionPatterns()
    .pattern({
        "aten::max_pool1d(Tensor self, int[1] kernel_size, int[1] stride=[], int[1] padding=[], int[1] dilation=[], bool ceil_mode=False) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            auto in = args[0].ITensor();
            auto shape = util::toVec(in->getDimensions());

            // Max Pool needs at least 4D input
            if (shape.size() < 4) {
                auto new_shape = util::toDimsPad(shape, 4);
                LOG_DEBUG("Input shape is less than 4D got: " << util::toDims(shape) << ", inserting shuffle layer to reshape to 4D tensor shape: " << new_shape);
                auto shuffle = ctx->net->addShuffle(*in);
                shuffle->setReshapeDimensions(new_shape);
                shuffle->setName((util::node_info(n) + " [Reshape to " + util::toStr(new_shape) + ']').c_str());
                in = shuffle->getOutput(0);
            }

            auto kernel_vec = args[1].unwrapToIntList().vec();
            kernel_vec.insert(kernel_vec.begin(), 1);
            auto kernel_size = util::toDims(kernel_vec);
            LOG_DEBUG("kernel_size: " << kernel_size);
            auto stride_vec = args[2].unwrapToIntList().vec();
            stride_vec.insert(stride_vec.begin(), 1);
            auto stride = util::toDims(stride_vec);
            LOG_DEBUG("stride: " << stride);
            auto padding_vec = args[3].unwrapToIntList().vec();
            padding_vec.insert(padding_vec.begin(), 0);
            auto padding = util::toDims(padding_vec);
            LOG_DEBUG("padding: " << padding);

            auto dilation = util::toDims(args[4].unwrapToIntList());

            TRTORCH_ASSERT(dilation == util::toDims(std::vector<int64_t>(dilation.nbDims, 1)), "Pooling dilation is not supported in TensorRT");

            LOG_DEBUG("dilation: " << dilation);
            LOG_WARNING("Dilation not used in max pooling converter");
            bool ceil_mode = args[5].unwrapToBool();

            auto new_layer = ctx->net->addPoolingNd(*in, nvinfer1::PoolingType::kMAX, kernel_size);
            TRTORCH_CHECK(new_layer, "Unable to create Max Pooling layer from node: " << *n);

            new_layer->setName(util::node_info(n).c_str());
            new_layer->setPaddingNd(padding);
            if (stride.nbDims != 2 && ctx->settings.device == nvinfer1::DeviceType::kDLA) {
                if (!ctx->settings.allow_gpu_fallback) {
                    TRTORCH_THROW_ERROR("DLA Pooling stride is limited to 2D, allow GPU fallback");
                } else {
                    LOG_WARNING("DLA Pooling stride is limited to 2D, will run on GPU");
                }
            }
            new_layer->setStrideNd(stride);

            auto padding_mode = ceil_mode ? nvinfer1::PaddingMode::kEXPLICIT_ROUND_UP :  nvinfer1::PaddingMode::kEXPLICIT_ROUND_DOWN;
            new_layer->setPaddingMode(padding_mode);

            new_layer->setName(util::node_info(n).c_str());
            auto out_tensor = ctx->AssociateValueAndTensor(n->outputs()[0], new_layer->getOutput(0));

            LOG_DEBUG("Output tensor shape: " << out_tensor->getDimensions());
            return true;
        }
    }).pattern({
        "aten::avg_pool1d(Tensor self, int[1] kernel_size, int[1] stride=[], int[1] padding=0, bool ceil_mode=False, bool count_include_pad=True) -> Tensor",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            auto in = args[0].ITensor();
            auto shape = util::toVec(in->getDimensions());

            // Avg Pool needs at least 4D input
            if (shape.size() < 4) {
                auto new_shape = util::toDimsPad(shape, 4);
                LOG_DEBUG("Input shape is less than 4D got: " << util::toDims(shape) << ", inserting shuffle layer to reshape to 4D tensor shape: " << new_shape);
                auto shuffle = ctx->net->addShuffle(*in);
                shuffle->setReshapeDimensions(new_shape);
                shuffle->setName((util::node_info(n) + " [Reshape to " + util::toStr(new_shape) + ']').c_str());
                in = shuffle->getOutput(0);
            }


            auto kernel_vec = args[1].unwrapToIntList().vec();
            kernel_vec.insert(kernel_vec.begin(), 1);
            auto kernel_size = util::toDims(kernel_vec);
            LOG_DEBUG("kernel_size: " << kernel_size);
            auto stride_vec = args[2].unwrapToIntList().vec();
            stride_vec.insert(stride_vec.begin(), 1);
            auto stride = util::toDims(stride_vec);
            LOG_DEBUG("stride: " << stride);
            auto padding_vec = args[3].unwrapToIntList().vec();
            padding_vec.insert(padding_vec.begin(), 0);
            auto padding = util::toDims(padding_vec);
            LOG_DEBUG("padding: " << padding);

            bool ceil_mode = args[4].unwrapToBool();
            bool count_inlcude_pad = args[5].unwrapToBool();

            auto new_layer = ctx->net->addPoolingNd(*in, nvinfer1::PoolingType::kAVERAGE, kernel_size);
            TRTORCH_CHECK(new_layer, "Unable to create Avg Pool 2D layer from node: " << *n);

            new_layer->setName(util::node_info(n).c_str());
            new_layer->setPaddingNd(padding);

            if (stride.nbDims != 2 && ctx->settings.device == nvinfer1::DeviceType::kDLA) {
                if (!ctx->settings.allow_gpu_fallback) {
                    TRTORCH_THROW_ERROR("DLA Pooling stride is limited to 2D, allow GPU fallback");
                } else {
                    LOG_WARNING("DLA Pooling stride is limited to 2D, will run on GPU");
                }
            }

            new_layer->setStrideNd(stride);

            auto padding_mode = ceil_mode ? nvinfer1::PaddingMode::kEXPLICIT_ROUND_UP :  nvinfer1::PaddingMode::kEXPLICIT_ROUND_DOWN;
            new_layer->setPaddingMode(padding_mode);
            new_layer->setAverageCountExcludesPadding(!count_inlcude_pad);

            new_layer->setName(util::node_info(n).c_str());
            auto out_tensor = ctx->AssociateValueAndTensor(n->outputs()[0], new_layer->getOutput(0));

            LOG_DEBUG("Output tensor shape: " << out_tensor->getDimensions());
            return true;
        }
    })
    .pattern({
        "aten::max_pool2d(Tensor self, int[2] kernel_size, int[2] stride=[], int[2] padding=[0, 0], int[2] dilation=[1, 1], bool ceil_mode=False) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            return MaxPoolingConverter(ctx, n, args);
        }
    }).pattern({
        "aten::avg_pool2d(Tensor self, int[2] kernel_size, int[2] stride=[], int[2] padding=[0, 0], bool ceil_mode=False, bool count_include_pad=True, int? divisor_override=None) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            return AvgPoolingConverter(ctx, n, args);
        }
    }).pattern({
        "aten::max_pool3d(Tensor self, int[3] kernel_size, int[3] stride=[], int[3] padding=[], int[3] dilation=[], bool ceil_mode=False) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            return MaxPoolingConverter(ctx, n, args);
        }
    }).pattern({
        "aten::avg_pool3d(Tensor self, int[3] kernel_size, int[3] stride=[], int[3] padding=[], bool ceil_mode=False, bool count_include_pad=True, int? divisor_override=None) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            return AvgPoolingConverter(ctx, n, args);
        }
    }).pattern({
        "aten::adaptive_avg_pool2d(Tensor self, int[2] output_size) -> (Tensor)",
        [](ConversionCtx* ctx, const torch::jit::Node* n, args& args) -> bool {
            auto in = args[0].ITensor();
            auto in_shape = util::toVec(in->getDimensions());

            if (in_shape.size() < 4) {
                auto new_shape = util::toDimsPad(in_shape, 4);
                LOG_DEBUG("Input shape is less than 4D got: " << util::toDims(in_shape) << ", inserting shuffle layer to reshape to 4D tensor shape: " << new_shape);
                auto shuffle = ctx->net->addShuffle(*in);
                shuffle->setReshapeDimensions(new_shape);
                shuffle->setName((util::node_info(n) + " [Reshape to " + util::toStr(new_shape) + ']').c_str());
                in = shuffle->getOutput(0);
                in_shape = util::toVec(in->getDimensions());
            }

            auto out_shape = args[1].IValue()->toIntList();

            std::vector<int64_t> stride(out_shape.size());
            for (size_t i = 0; i < out_shape.size(); i++) {
                stride[(stride.size() - 1) - i] = in_shape[(in_shape.size() - 1) - i] / out_shape[(out_shape.size() - 1) - i];
            }
            LOG_DEBUG("Stride: " << util::toDims(stride));

            std::vector<int64_t> window(out_shape.size());
            for (size_t i = 0; i < out_shape.size(); i++) {
                window[window.size() - 1 - i] = in_shape[in_shape.size() - 1 - i] - (out_shape[out_shape.size() - 1 - i] - 1) * stride[stride.size() - 1 - i];
            }

            LOG_DEBUG("Window: " << util::toDims(window));

            auto new_layer = ctx->net->addPoolingNd(*in, nvinfer1::PoolingType::kAVERAGE, util::toDims(window));
            TRTORCH_CHECK(new_layer, "Unable to create average pooling layer from node: " << *n);

            new_layer->setStrideNd(util::toDims(stride));

            new_layer->setName(util::node_info(n).c_str());
            auto out_tensor = ctx->AssociateValueAndTensor(n->outputs()[0], new_layer->getOutput(0));

            LOG_DEBUG("Output tensor shape: " << out_tensor->getDimensions());
            return true;
        }
    });
} // namespace
} // namespace impl
} // namespace converters
} // namespace conversion
} // namespace core
} // trtorch
