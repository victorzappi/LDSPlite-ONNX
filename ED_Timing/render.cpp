/*
    Neural model taken from:
    Simionato, Riccardo, and Stefano Fasciani. "Fully Conditioned and Low-latency Black-box Modeling of Analog Compression." In Proceedings of the International Conference on Digital Audio Effects. DAFx Board, 2023.
    https://github.com/RiccardoVib/CONDITIONED-MODELING-OF-OPTICAL-COMPRESSOR
*/

#include "LDSP.h"
#include "libraries/OrtModel/OrtModel.h"
#include <fstream>

OrtModel model;
std::string modelType = "onnx";
std::string modelName = "ED";

const int w = 16;
const int u = 64;
const int d = 4;

float input[2*w] = {0};
float params[d] = {0};
float output[w] = {0};

int inputSize = 2*w;
int outputSize = w;
int inputCounter = 0;

const int circBuffLength = 48000; // a reasonably large buffer, to limit end-of-buffer overhead
int writePointer;
int readPointer;
float circBuff[circBuffLength];

//--------------------------------

unsigned long long * inferenceTimes;
int logPtr = 0;
constexpr int testDuration_sec = 10;
int numLogs;


bool setup(LDSPcontext *context, void *userData)
{
  std::string modelPath = modelName+"."+modelType;
  if (!model.setup("session1", modelPath))
    LDSP_log("unable to setup ortModel");

  writePointer = w; // the first w samples must be zeros
  readPointer = 0;

  if(context->audioFrames % w)
    LDSP_log("Warning! Period size (%d) is supposed to be an integer multiple of the output size w (%d)!\n", context->audioFrames, outputSize);

  //--------------------------------
  inferenceTimes = new unsigned long long[context->audioSampleRate*testDuration_sec*1.01];
  numLogs = context->audioSampleRate*testDuration_sec / outputSize; // division to handle case of models outputting a block of samples

  return true;
}

void render(LDSPcontext *context, void *userData)
{
  for(int n=0; n<context->audioFrames; n++)
  {
    circBuff[writePointer] = audioRead(context,n,0);

    // run inference every w inputs
    if( ++inputCounter == outputSize )
    {
      inputCounter = 0;

      if(readPointer<=circBuffLength-inputSize)
        std::copy(circBuff + readPointer, circBuff + readPointer + inputSize, input);
      else
      {
        int firstPartSize = circBuffLength - readPointer;
        std::copy(circBuff + readPointer, circBuff + circBuffLength, input);
        std::copy(circBuff, circBuff + (inputSize - firstPartSize), input + firstPartSize);
      }

      // Start the Clock
      auto start_time = std::chrono::high_resolution_clock::now();

      model.run(input, params, output); // outputs a block of w samples

      // Stop the clock
      auto end_time = std::chrono::high_resolution_clock::now();
      inferenceTimes[logPtr] = std::chrono::duration_cast
          <std::chrono::microseconds>(end_time - start_time).count();
      logPtr++;

      for(int out=0; out<outputSize; out++)
      {
        // passthrough test, because the model may not be trained
        audioWrite(context, n-outputSize+1+out, 0, input[outputSize+out]);
        audioWrite(context, n-outputSize+1+out, 1, input[outputSize+out]);
      }

      readPointer += outputSize;
      if( readPointer >= circBuffLength)
        readPointer -= circBuffLength;
    }
    if(++writePointer >= circBuffLength)
      writePointer = 0;

    if(logPtr>=numLogs)
      LDSP_requestStop();
  }
}
void cleanup(LDSPcontext *context, void *userData)
{
  std::string timingLogDir = "/sdcard";//"/data/user/0/com.ldsp.ldsplite/files";
  std::string timingLogFileName = "inferenceTiming_"+modelName+"_out"+std::to_string(outputSize)+"_onnx.txt";
  std::string timingLogFilePath = timingLogDir+"/"+timingLogFileName;

  std::ofstream logFile(timingLogFilePath);
  if(logFile.is_open())
  {
    for(int i=0;i<numLogs; i++)
      logFile << std::to_string(inferenceTimes[i]) << "\n";
  }
  logFile.close();

  delete[] inferenceTimes;

  model.cleanup();
}