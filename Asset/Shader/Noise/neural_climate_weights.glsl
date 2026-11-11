// Placeholder model. Tools/ClimateNeural/train_climate.py replaces this file.
// The neutral output keeps the Climate Noise Lab compilable before training.
#ifndef ENDURANCE_NEURAL_CLIMATE_WEIGHTS_GLSL
#define ENDURANCE_NEURAL_CLIMATE_WEIGHTS_GLSL

#define NEURAL_CLIMATE_MODEL_READY 0
#define NEURAL_CLIMATE_INPUT_COUNT 16
#define NEURAL_CLIMATE_HIDDEN0_COUNT 1
#define NEURAL_CLIMATE_HIDDEN1_COUNT 1
#define NEURAL_CLIMATE_OUTPUT_COUNT 2

const float NEURAL_CLIMATE_W0[16] = float[16](
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
);
const float NEURAL_CLIMATE_B0[1] = float[1](0.0);
const float NEURAL_CLIMATE_W1[1] = float[1](0.0);
const float NEURAL_CLIMATE_B1[1] = float[1](0.0);
const float NEURAL_CLIMATE_W2[2] = float[2](0.0, 0.0);
const float NEURAL_CLIMATE_B2[2] = float[2](0.0, 0.0);

#endif
