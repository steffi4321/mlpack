/**
 * @file methods/ann/ffn_impl.hpp
 * @author Marcus Edel
 *
 * Definition of the FFN class, which implements feed forward neural networks.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */
#ifndef MLPACK_METHODS_ANN_FFN_IMPL_HPP
#define MLPACK_METHODS_ANN_FFN_IMPL_HPP

// In case it hasn't been included yet.
#include "ffn.hpp"

#include "util/gradient_update.hpp"
#include "util/deterministic_update.hpp"
#include "util/output_width_update.hpp"
#include "util/output_height_update.hpp"

namespace mlpack {
namespace ann /** Artificial Neural Network. */ {

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::FFN(
    OutputLayerType outputLayer, InitializationRuleType initializeRule) :
    outputLayer(std::move(outputLayer)),
    initializeRule(std::move(initializeRule)),
    reset(false),
    numFunctions(0),
    deterministic(false)
{
  /* Nothing to do here. */
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::~FFN()
{
  for (size_t i = 0; i < network.size(); ++i)
    delete network[i];
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
    ResetData(InputType predictors, InputType responses)
{
  numFunctions = responses.n_cols;
  this->predictors = std::move(predictors);
  this->responses = std::move(responses);
  this->deterministic = false;
  ResetDeterministic();

  if (!reset)
    ResetParameters();
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename OptimizerType>
typename std::enable_if<
      HasMaxIterations<OptimizerType, size_t&(OptimizerType::*)()>
      ::value, void>::type
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
WarnMessageMaxIterations(OptimizerType& optimizer, size_t samples) const
{
  if (optimizer.MaxIterations() < samples &&
      optimizer.MaxIterations() != 0)
  {
    Log::Warn << "The optimizer's maximum number of iterations "
        << "is less than the size of the dataset; the "
        << "optimizer will not pass over the entire "
        << "dataset. To fix this, modify the maximum "
        << "number of iterations to be at least equal "
        << "to the number of points of your dataset "
        << "(" << samples << ")." << std::endl;
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename OptimizerType>
typename std::enable_if<
      !HasMaxIterations<OptimizerType, size_t&(OptimizerType::*)()>
      ::value, void>::type
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
WarnMessageMaxIterations(OptimizerType& /* optimizer */, size_t /* samples */)
    const
{
  // Nothing to do here.
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename OptimizerType, typename... CallbackTypes>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
    Train(InputType predictors, InputType responses, OptimizerType& optimizer,
        CallbackTypes&&... callbacks)
{
  ResetData(std::move(predictors), std::move(responses));

  WarnMessageMaxIterations<OptimizerType>(optimizer, this->predictors.n_cols);

  // Train the model.
  Timer::Start("ffn_optimization");
  const double out = optimizer.Optimize(*this, parameters, callbacks...);
  Timer::Stop("ffn_optimization");

  Log::Info << "FFN::FFN(): final objective of trained model is " << out
      << "." << std::endl;
  return out;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename OptimizerType, typename... CallbackTypes>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
    Train(InputType predictors, InputType responses,
        CallbackTypes&&... callbacks)
{
  ResetData(std::move(predictors), std::move(responses));

  OptimizerType optimizer;

  WarnMessageMaxIterations<OptimizerType>(optimizer, this->predictors.n_cols);

  // Train the model.
  Timer::Start("ffn_optimization");
  const double out = optimizer.Optimize(*this, parameters, callbacks...);
  Timer::Stop("ffn_optimization");

  Log::Info << "FFN::FFN(): final objective of trained model is " << out
      << "." << std::endl;
  return out;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename PredictorsType, typename ResponsesType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Forward(const PredictorsType& inputs, ResponsesType& results)
{
  Forward(inputs, results, 0, network.size() - 1);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename PredictorsType, typename ResponsesType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Forward(const PredictorsType& inputs,
        ResponsesType& results,
        const size_t begin,
        const size_t end)
{
  // Sanity checking...
  if (end < begin)
    return;

  // This is the function that actually runs the data through the network.  It
  // is here that we need to check that the network is initialized correctly.

  if (parameters.is_empty())
  {
    // This does network initialization.
    ResetParameters();
  }

  // Next we need to ensure that space for layerOutputs is allocated correctly.
  InitializeForwardPassMemory(inputs.n_cols);

  // TODO: what is reset?
  if (!reset)
  {
    // Assume the input is flat...
    std::vector<size_t> dims;
    dims.push_back(inputs.n_rows);
    dims.push_back(1);

    network.front()->InputDimensions() = dims;
    for (size_t i = 1; i < network.size(); ++i)
    {
      network[i]->InputDimensions() = network[i - 1]->OutputDimensions();
    }

    reset = true;
  }

  // Ensure that the results matrix is the right size.
  results.set_size(inputs.n_cols * network[end]->OutputSize());

  if (end > begin)
  {
    network[begin]->Forward(inputs, layerOutputs[begin]);

    for (size_t i = 1; i < end - begin; ++i)
    {
      network[begin + i]->Forward(layerOutputs[begin + i - 1],
          layerOutputs[begin + i]);
    }

    network[end]->Forward(layerOutputs[end - 1], results);
  }
  else
  {
    network[end]->Forward(inputs, results);
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename PredictorsType, typename TargetsType, typename GradientsType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Backward(const PredictorsType& inputs,
         const TargetsType& targets,
         GradientsType& gradients)
{
  // We assume that Forward() has already been called!
  double res = outputLayer.Forward(layerOutputs[network.size() - 1], targets);

  for (size_t i = 0; i < network.size(); ++i)
    res += network[i]->Loss();

  // error's size will be set correctly by outputLayer.Backward().
  outputLayer.Backward(layerOutputs[network.size() - 1], targets, error);

  gradients = arma::zeros<GradientsType>(parameters.n_rows, parameters.n_cols);

  Backward();
  Gradient(inputs, gradients);

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Predict(InputType predictors, OutputType& results, const size_t batchSize)
{
  // TODO: actually care about batchSize

  if (parameters.is_empty())
    ResetParameters();

  if (!deterministic)
  {
    deterministic = true;
    ResetDeterministic();
  }

  results.set_size(network.back()->OutputSize(), predictors.n_cols);

  for (size_t i = 0; i < predictors.n_cols; ++i)
  {
    InputType predictorAlias(predictors.colptr(i), predictors.n_rows, 1, false,
        true);
    OutputType resultAlias(results.colptr(i), results.n_rows, 1, false, true);

    Forward(predictorAlias, resultAlias);
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename PredictorsType, typename ResponsesType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Evaluate(const PredictorsType& predictors, const ResponsesType& responses)
{
  if (parameters.is_empty())
    ResetParameters();

  if (!deterministic)
  {
    deterministic = true;
    ResetDeterministic();
  }

  // Note that layerOutputs may not yet be initialized correctly, but if it
  // isn't, this will be handled by Forward().
  Forward(predictors, layerOutputs.back());

  double res = outputLayer.Forward(layerOutputs.back(), responses);
  for (size_t i = 0; i < network.size(); ++i)
    res += LossUpdate(network[i]);

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
    Evaluate(const OutputType& parameters)
{
  double res = 0;
  for (size_t i = 0; i < predictors.n_cols; ++i)
    res += Evaluate(parameters, i, 1, true);

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Evaluate(const OutputType& /* parameters */,
         const size_t begin,
         const size_t batchSize,
         const bool deterministic)
{
  if (parameters.is_empty())
    ResetParameters();

  if (deterministic != this->deterministic)
  {
    this->deterministic = deterministic;
    ResetDeterministic();
  }

  Forward(predictors.cols(begin, begin + batchSize - 1), layerOutputs.back());

  double res = outputLayer.Forward(layerOutputs.back(),
      responses.cols(begin, begin + batchSize - 1));

  for (size_t i = 0; i < network.size(); ++i)
    res += network[i]->Loss();

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Evaluate(const OutputType& parameters,
         const size_t begin,
         const size_t batchSize)
{
  return Evaluate(parameters, begin, batchSize, true);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
    EvaluateWithGradient(const OutputType& parameters, OutputType& gradient)
{
  double res = 0;
  for (size_t i = 0; i < predictors.n_cols; ++i)
    res += EvaluateWithGradient(parameters, i, gradient, 1);

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
double FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
EvaluateWithGradient(const OutputType& parameters,
                     const size_t begin,
                     OutputType& gradient,
                     const size_t batchSize)
{
  if (gradient.is_empty())
  {
    gradient = arma::zeros<OutputType>(parameters.n_rows, parameters.n_cols);
  }
  else
  {
    gradient.zeros();
  }

  // TODO: can this just be a call to Forward() then Backward()?

  double res = Evaluate(parameters, begin, batchSize, false);

  for (size_t i = 0; i < network.size(); ++i)
    res += network[i]->Loss();

  outputLayer.Backward(layerOutputs.back(),
      responses.cols(begin, begin + batchSize - 1),
      error);

  Backward();
  Gradient(predictors.cols(begin, begin + batchSize - 1), gradient);

  return res;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Gradient(const OutputType& parameters,
         const size_t begin,
         OutputType& gradient,
         const size_t batchSize)
{
  this->EvaluateWithGradient(parameters, begin, gradient, batchSize);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Shuffle()
{
  math::ShuffleData(predictors, responses, predictors, responses);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
ResetParameters()
{
  ResetDeterministic();

  // Reset the network parameter with the given initialization rule.
  NetworkInitialization<InitializationRuleType> networkInit(initializeRule);
  networkInit.Initialize(network, parameters);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
ResetDeterministic()
{
  // TODO: change the name of this...
  for (size_t i = 0; i < network.size(); ++i)
    network[i]->Deterministic() = true;
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
ResetGradients(OutputType& gradient)
{
  size_t offset = 0;
  for (size_t i = 0; i < network.size(); ++i)
    offset += GradientUpdate(network[i], gradient, offset);
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Backward()
{
  // This actually does the backward computation.  In order to do that, we need
  // to make sure that the local variables we'll be using are properly
  // initialized.
  InitializeBackwardPassMemory(layerOutputs.back().n_cols);

  // layerDeltas.back() will have size
  // network[network.size() - 2]->OutputSize() * batchSize.
  network.back()->Backward(layerOutputs.back(), error, layerDeltas.back());

  for (size_t i = 1; i < network.size(); ++i)
  {
    network[network.size() - i]->Backward(
        layerOutputs[network.size() - i],
        layerDeltas[network.size() - i + 1],
        layerDeltas[network.size() - i]);
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Gradient(const InputType& input, OutputType& gradient)
{
  // Make sure that the memory is initialized for layerGradients.
  InitializeGradientPassMemory(gradient);

  network.front()->Gradient(input, layerDeltas[1], layerGradients.front());

  for (size_t i = 1; i < network.size() - 1; ++i)
  {
    network[i]->Gradient(layerOutputs[i - 1], layerDeltas[i + 1],
        layerGradients[i]);
  }

  network.back()->Gradient(layerOutputs[network.size() - 2], error,
      layerGradients.back());
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
template<typename Archive>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
serialize(Archive& ar, const uint32_t /* version */)
{
  // Serialize the output layer and initialization rule.
  ar(CEREAL_NVP(outputLayer));
  ar(CEREAL_NVP(initializeRule));

  // Serialize the network itself.
  ar(CEREAL_VECTOR_POINTER(network));
  ar(CEREAL_NVP(parameters));

  // Serialize the expected input size.
  ar(CEREAL_NVP(inputDimensions));
  ar(CEREAL_NVP(reset));

  // If we are loading, we need to initialize the weights.
  if (cereal::is_loading<Archive>())
  {
    // We can clear these members, since it's not possible to serialize in the
    // middle of training and resume.
    predictors.clear();
    responses.clear();
    numFunctions = 0;

    layerOutputMatrix.clear();
    layerOutputs.clear();
    layerOutputs.resize(network.size(), OutputType());

    deltaMatrix.clear();
    layerDeltas.clear();
    layerDeltas.resize(network.size(), OutputType());

    gradientMatrix.clear();
    layerGradients.clear();
    layerGradients.resize(network.size(), OutputType());

    deterministic = true;

    // Reset each layer so its weights are set right.
    // TODO: should we just do that in serialize()?
//    for (size_t i = 0; i < network.size(); ++i)
//      network[i]->Reset();
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
Swap(FFN& network)
{
  std::swap(outputLayer, network.outputLayer);
  std::swap(initializeRule, network.initializeRule);
  std::swap(reset, network.reset);
  std::swap(this->network, network.network);
  std::swap(parameters, network.parameters);
  std::swap(inputDimensions, network.inputDimensions);
  std::swap(predictors, network.predictors);
  std::swap(responses, network.responses);
  std::swap(numFunctions, network.numFunctions);
  std::swap(error, network.error);
  std::swap(deterministic, network.deterministic);
  std::swap(layerOutputMatrix, network.layerOutputMatrix);
  std::swap(totalOutputSize, network.totalOutputSize);
  std::swap(layerOutputs, network.layerOutputs);
  std::swap(deltaMatrix, network.deltaMatrix);
  std::swap(layerDeltas, network.layerDeltas);
  std::swap(gradientMatrix, network.gradientMatrix);
  std::swap(layerGradients, network.layerGradients);
};

// Initialize memory to be used for storing the outputs of each layer, if
// necessary.  This should be called at the start of each forward pass, but does
// not need to be called at any other time.
template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
InitializeForwardPassMemory(const size_t batchSize)
{
  // We need to initialize memory to store the output of each layer's Forward()
  // call.  We'll do this all in one matrix, but, the size of this matrix
  // depends on the batch size we are using for computation.  We avoid resizing
  // layerOutputMatrix down, unless we only need 10% or less of it.
  if (batchSize * totalOutputSize > layerOutputMatrix.n_elem ||
      batchSize * totalOutputSize < std::floor(0.1 * layerOutputMatrix.n_elem))
  {
    // All outputs will be represented by one big block of memory.
    layerOutputMatrix = OutputType(1, batchSize * totalOutputSize);
  }

  // Now, create an alias to the right place for each layer.  We assume that
  // layerOutputs is already sized correctly (this should be done by Add()).
  size_t start = 0;
  for (size_t i = 0; i < layerOutputs.size(); ++i)
  {
    const size_t layerOutputSize = network[i]->OutputSize();
    layerOutputs[i] = OutputType(layerOutputMatrix.colptr(start),
        layerOutputSize, batchSize, false, true);
    start += batchSize * layerOutputSize;
  }
}

// Initialize memory to be used for the backward pass.  This should be called at
// the start of each backward pass, but does not need to be called at any other
// time.
template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
InitializeBackwardPassMemory(const size_t batchSize)
{
  // We need to initialize memory to store the output of each layer's Backward()
  // and Gradient() calls.  We do this similarly to
  // InitializeForwardPassMemory(), but we must store a matrix to use as the
  // delta for each layer.
  const size_t inputSize = std::accumulate(inputDimensions.begin(),
      inputDimensions.end(), 0);
  const size_t totalInputSize = totalOutputSize - network.back()->OutputSize() +
      inputSize;
  if (batchSize * totalInputSize > deltaMatrix.n_elem ||
      batchSize * totalInputSize < std::floor(0.1 * layerOutputMatrix.n_elem))
  {
    // All deltas will be represented by one big block of memory.
    deltaMatrix = OutputType(1, batchSize * totalInputSize);
  }

  // Now, create an alias to the right place for each layer.  We assume that
  // layerDeltas is already sized correctly (this should be done by Add()).
  size_t start = 0;
  for (size_t i = 0; i < layerDeltas.size(); ++i)
  {
    const size_t layerInputSize = (i == 0) ? inputSize :
        network[i - 1]->OutputSize();
    layerDeltas[i] = OutputType(deltaMatrix.colptr(start), layerInputSize,
        batchSize, false, true);
    start += batchSize * layerInputSize;
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
void FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
InitializeGradientPassMemory(OutputType& gradient)
{
  // We need to initialize the aliases `layerGradients()` for each layer.
  size_t start = 0;
  for (size_t i = 0; i < layerGradients.size(); ++i)
  {
    const size_t layerParamSize = network[i]->WeightSize();
    layerGradients[i] = OutputType(gradient.colptr(start), layerParamSize, 1,
        false, true);
  }
}

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::FFN(
    const FFN& network):
    outputLayer(network.outputLayer),
    initializeRule(network.initializeRule),
    reset(network.reset),
    parameters(network.parameters),
    inputDimensions(network.inputDimensions),
    predictors(network.predictors),
    responses(network.responses),
    numFunctions(network.numFunctions),
    error(network.error),
    deterministic(network.deterministic)
{
  // Build new layers according to source network
  for (size_t i = 0; i < network.network.size(); ++i)
  {
    this->network.push_back(network.network[i]->Clone());
    // TODO:  dig into this
    // this will need the weights to be set right for each layer...
    //ResetUpdate(this->network.back());
  }

  layerOutputs.resize(this->network.size(), OutputType());
  totalOutputSize = network.totalOutputSize;
  layerDeltas.resize(this->network.size(), OutputType());
  layerGradients.resize(this->network.size(), OutputType());
};

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::FFN(
    FFN&& network):
    outputLayer(std::move(network.outputLayer)),
    initializeRule(std::move(network.initializeRule)),
    reset(network.reset),
    network(std::move(network.network)),
    parameters(std::move(network.parameters)),
    inputDimensions(std::move(network.inputDimensions)),
    predictors(std::move(network.predictors)),
    responses(std::move(network.responses)),
    numFunctions(network.numFunctions),
    error(std::move(network.error)),
    deterministic(network.deterministic),
    layerOutputMatrix(std::move(network.layerOutputMatrix)),
    layerOutputs(std::move(network.layerOutputs)),
    deltaMatrix(std::move(network.deltaMatrix)),
    layerDeltas(std::move(network.layerDeltas)),
    gradientMatrix(std::move(network.gradientMatrix)),
    layerGradients(std::move(network.layerGradients))
{
  // Nothing else to do.
};

template<typename OutputLayerType,
         typename InitializationRuleType,
         typename InputType,
         typename OutputType>
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>&
FFN<OutputLayerType, InitializationRuleType, InputType, OutputType>::
operator=(FFN network)
{
  Swap(network);
  return *this;
};

} // namespace ann
} // namespace mlpack

#endif
