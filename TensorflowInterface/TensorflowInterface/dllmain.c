// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

// Based on example by Patrick Wieschollek <mail@patwie.com>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/tensorflow/c/c_api.h"

TF_Session* sess;
TF_Graph* graph;
TF_Status* status;
TF_Buffer* graph_def;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		sess = NULL;
		graph = NULL;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


TF_Buffer* read_file(const char* file);

void write_file(const char* data, int bytes, const char* file);

void free_buffer(void* data, size_t length) { free(data); }

//void deallocator(void* ptr, size_t len, void* arg) { free((void*)ptr); }
void deallocator(void* ptr, size_t len, void* arg) { return; } // up to caller to deallocate

int initialize_global_session(const char* graph_path) {
	// load graph
	// ================================================================================
	char msgbuf[101];
	graph_def = read_file(graph_path);
	graph = TF_NewGraph();
	status = TF_NewStatus();
	TF_ImportGraphDefOptions* opts = TF_NewImportGraphDefOptions();
	TF_GraphImportGraphDef(graph, graph_def, opts, status);
	TF_DeleteImportGraphDefOptions(opts);
	if (TF_GetCode(status) != TF_OK) {
		snprintf(msgbuf, 100, "ERROR: Unable to import graph %s", TF_Message(status));
		OutputDebugStringA(msgbuf);
		return -1;
	}
	OutputDebugStringA("Successfully imported graph");

	// create session
	// ================================================================================
	TF_SessionOptions* opt = TF_NewSessionOptions();
	sess = TF_NewSession(graph, opt, status);
	TF_DeleteSessionOptions(opt);
	if (TF_GetCode(status) != TF_OK) {
		snprintf(msgbuf, 100, "ERROR: Unable to create session %s", TF_Message(status));
		OutputDebugStringA(msgbuf);
		return -1;
	}
	OutputDebugStringA("Successfully created session");
	return 0;
}

void close_global_session() {

	TF_DeleteGraph(graph);
	TF_DeleteBuffer(graph_def);
	TF_CloseSession(sess, status);
	TF_DeleteSession(sess, status);
	TF_DeleteStatus(status);
}

int image_and_fmaps_from_latent(int num_inputs, float** inputs, int* input_num_dims, int** input_dims, const char** input_names,
	int num_outputs, float** outputs, int* output_num_dims, int** output_dims, const char** output_names, const char* out_path) {
	OutputDebugStringA("Creating image from latent");
	char msgbuf[101];
	if (sess == NULL)
	{
		OutputDebugStringA("ERROR: Call initialize_global_session first");
		return -1;
	}

	TF_Output* run_inputs = (TF_Output*)malloc(num_inputs * sizeof(TF_Output));
	TF_Tensor** run_inputs_tensors = (TF_Tensor * *)malloc(num_inputs * sizeof(TF_Tensor*));
	OutputDebugStringA("Setting up inputs");
	for (int i = 0; i < num_inputs; i++)
	{
		TF_Operation* input_op = TF_GraphOperationByName(graph, input_names[i]);
		int64_t* raw_input_dims = (int64_t*)malloc(input_num_dims[i] * sizeof(int64_t));
		size_t total_size = 1;
		for (int j = 0; j < input_num_dims[i]; j++)
		{
			raw_input_dims[j] = input_dims[i][j];
			total_size *= input_dims[i][j];
		}
		TF_Tensor* input_tensor =
			TF_NewTensor(TF_FLOAT, raw_input_dims, input_num_dims[i], inputs[i],
				total_size * sizeof(float), &deallocator, NULL);
		run_inputs[i].oper = input_op;
		run_inputs[i].index = 0;
		run_inputs_tensors[i] = input_tensor;
	}
	

	int max_fsize = 5 * 10000 * 100;
	TF_Output* run_outputs = (TF_Output*)malloc(num_outputs * sizeof(TF_Output));
	TF_Tensor** run_output_tensors = (TF_Tensor * *)malloc(num_outputs * sizeof(TF_Tensor*));
	OutputDebugStringA("Setting up outputs");
	for (int i = 0; i < num_outputs; i++)
	{
		TF_Operation* output_op = TF_GraphOperationByName(graph, output_names[i]);
		run_outputs[i].oper = output_op;
		run_outputs[i].index = 0;
		if (i == 0)
		{
			float* raw_output_data = (float*)malloc(max_fsize * sizeof(float));
			raw_output_data[i] = 1.f;
			TF_Tensor* output_tensor =
				TF_NewTensor(TF_STRING, NULL, 0, raw_output_data,
					max_fsize * sizeof(float), &deallocator, NULL);
			run_output_tensors[i] = output_tensor;
		}
		else
		{
			int64_t* raw_output_dims = (int64_t*)malloc(output_num_dims[i] * sizeof(int64_t));
			size_t total_size = 1;
			for (int j = 0; j < output_num_dims[i]; j++)
			{
				raw_output_dims[j] = output_dims[i][j];
				total_size *= output_dims[i][j];
			}
			float* raw_output_data = (float*)malloc(total_size * sizeof(float));
			raw_output_data[i] = 1.f;
			TF_Tensor* output_tensor =
				TF_NewTensor(TF_FLOAT, raw_output_dims, output_num_dims[i], raw_output_data,
					total_size * sizeof(float), &deallocator, NULL);
			run_output_tensors[i] = output_tensor;
		}
	}

	// run network
	// ================================================================================
	OutputDebugStringA("Session running...");
	TF_SessionRun(sess,
		/* RunOptions */ NULL,
		/* Input tensors */ run_inputs, run_inputs_tensors, num_inputs,
		/* Output tensors */ run_outputs, run_output_tensors, num_outputs,
		/* Target operations */ NULL, 0,
		/* RunMetadata */ NULL,
		/* Output status */ status);
	OutputDebugStringA("Session finished");

	if (TF_GetCode(status) != TF_OK) {
		snprintf(msgbuf, 100, "ERROR: Unable to run output_op: %s", TF_Message(status));
		OutputDebugStringA(msgbuf);
		return -1;
	}


	void* output_image = TF_TensorData(run_output_tensors[0]);
	for (int i = 1; i < num_outputs; i++)
	{
		snprintf(msgbuf, 100, "write byte size %zd", TF_TensorByteSize(run_output_tensors[i]));
		OutputDebugStringA(msgbuf);
		memcpy(outputs[i], TF_TensorData(run_output_tensors[i]), TF_TensorByteSize(run_output_tensors[i]));
	}

	OutputDebugStringA("Writing output");
	int byte_size = (int)(TF_TensorByteSize(run_output_tensors[0])) - 11;
	if (byte_size < 0)
	{
		snprintf(msgbuf, 100, "Invalid byte size %d", byte_size);
		OutputDebugStringA(msgbuf);
	}
	// Not sure why png starts 11 bytes from start of the output
	write_file((char*)output_image + 11, byte_size, out_path);
	OutputDebugStringA("Cleaning up");

	free((void*)run_inputs);
	free((void*)run_outputs);

	free((void*)run_inputs_tensors);
	free((void*)run_output_tensors);

	// todo: determine which memory needs to be freed and which is handled by tensorflow
	return 0;
}

int image_from_latent(float* latent, const char* input_tensor_name, const char* out_path,
	int fmap_channels, int fmap_height, int fmap_width, float* fmaps, const char* fmap_name) {

	OutputDebugStringA("Creating image from latent");
	char msgbuf[101];
	if (sess == NULL)
	{
		OutputDebugStringA("ERROR: Call initialize_global_session first");
		return -1;
	}

	if (strcmp(fmap_name, "") != 0)
	{
		OutputDebugStringA(fmap_name);
	}

	TF_Operation* input_op = TF_GraphOperationByName(graph, input_tensor_name);

	float* raw_input_data = latent;
	int64_t* raw_input_dims = (int64_t*)malloc(2 * sizeof(int64_t));
	if (raw_input_dims == NULL)
	{
		return -1;
	}
	raw_input_dims[0] = 1;
	raw_input_dims[1] = 512;

	/*
	TF_CAPI_EXPORT extern TF_Tensor* TF_NewTensor(
		TF_DataType,
		const int64_t* dims, int num_dims,
		void* data, size_t len,
		void (*deallocator)(void* data, size_t len, void* arg),
		void* deallocator_arg);
	*/
	// prepare inputs
	TF_Tensor* input_tensor =
		TF_NewTensor(TF_FLOAT, raw_input_dims, 2, latent,
			512 * sizeof(float), &deallocator, NULL);


	TF_Output* run_inputs = (TF_Output*)malloc(1 * sizeof(TF_Output));
	if (run_inputs == NULL)
	{
		return -1;
	}
	run_inputs[0].oper = input_op;
	run_inputs[0].index = 0;

	TF_Tensor** run_inputs_tensors = (TF_Tensor * *)malloc(1 * sizeof(TF_Tensor*));
	if (run_inputs_tensors == NULL)
	{
		return -1;
	}
	run_inputs_tensors[0] = input_tensor;

	// prepare outputs
	// ================================================================================
	TF_Operation* output_op = TF_GraphOperationByName(graph, "output");
	// printf("output_op has %i outputs\n", TF_OperationNumOutputs(output_op));

	// todo: figure out the right size
	int max_fsize = 5 * 10000 * 100;
	TF_Output* run_outputs = (TF_Output*)malloc(max_fsize * sizeof(TF_Output));
	if (run_outputs == NULL)
	{
		return -1;
	}
	run_outputs[0].oper = output_op;
	run_outputs[0].index = 0;

	TF_Tensor** run_output_tensors = (TF_Tensor * *)malloc(max_fsize * sizeof(TF_Tensor*));
	if (run_output_tensors == NULL)
	{
		return -1;
	}
	float* raw_output_data = (float*)malloc(max_fsize * sizeof(float));
	if (raw_output_data == NULL)
	{
		return -1;
	}
	raw_output_data[0] = 1.f;

	TF_Tensor* output_tensor =
		TF_NewTensor(TF_STRING, NULL, 0, raw_output_data,
			max_fsize * sizeof(float), &deallocator, NULL);
	run_output_tensors[0] = output_tensor;


	// run network
	// ================================================================================
	OutputDebugStringA("Session running...");
	TF_SessionRun(sess,
		/* RunOptions */ NULL,
		/* Input tensors */ run_inputs, run_inputs_tensors, 1,
		/* Output tensors */ run_outputs, run_output_tensors, 1,
		/* Target operations */ NULL, 0,
		/* RunMetadata */ NULL,
		/* Output status */ status);
	OutputDebugStringA("Session finished");

	if (TF_GetCode(status) != TF_OK) {
		snprintf(msgbuf, 100, "ERROR: Unable to run output_op: %s", TF_Message(status));
		OutputDebugStringA(msgbuf);
		return -1;
	}


	void* output_data = TF_TensorData(run_output_tensors[0]);

	OutputDebugStringA("Writing output");
	int byte_size = (int)(TF_TensorByteSize(run_output_tensors[0])) - 11;
	if (byte_size < 0)
	{
		snprintf(msgbuf, 100, "Invalid byte size %d", byte_size);
		OutputDebugStringA(msgbuf);
	}
	// Not sure why png starts 11 bytes from start of the output
	write_file((char*)output_data + 11, byte_size, out_path);
	OutputDebugStringA("Cleaning up");

	free((void*)run_inputs);
	free((void*)run_outputs);

	free((void*)run_inputs_tensors);
	free((void*)run_output_tensors);

	free((void*)raw_input_dims);
	return 0;
}

int generate_interemdiate_latent(float* out_intermediate_latent, const char* graph_path) {
	char msgbuf[101];
	if (sess == NULL)
	{
		OutputDebugStringA("ERROR: Call initialize_global_session first");
		return -1;
	}


	// prepare outputs
	// ================================================================================
	TF_Operation* output_op = TF_GraphOperationByName(graph, "intermediate_latent");
	// printf("output_op has %i outputs\n", TF_OperationNumOutputs(output_op));

	// todo: figure out the right size
	TF_Output* run_outputs = (TF_Output*)malloc(512 * sizeof(TF_Output));
	if (run_outputs == NULL)
	{
		return -1;
	}
	run_outputs[0].oper = output_op;
	run_outputs[0].index = 0;

	TF_Tensor** run_output_tensors = (TF_Tensor * *)malloc(512 * sizeof(TF_Tensor*));
	if (run_output_tensors == NULL)
	{
		return -1;
	}
	float* raw_output_data = (float*)malloc(512 * sizeof(float));
	if (raw_output_data == NULL)
	{
		return -1;
	}
	raw_output_data[0] = 1.f;
	int64_t* raw_output_dims = (int64_t*)malloc(512 * sizeof(int64_t));
	if (raw_output_dims == NULL)
	{
		return -1;
	}
	raw_output_dims[0] = 512;

	TF_Tensor* output_tensor =
		TF_NewTensor(TF_FLOAT, raw_output_dims, 1, raw_output_data,
			512 * sizeof(float), &deallocator, NULL);
	run_output_tensors[0] = output_tensor;


	// run network
	// ================================================================================
	OutputDebugStringA("Session running...");
	TF_SessionRun(sess,
		/* RunOptions */ NULL,
		/* Input tensors */ NULL, NULL, 0,
		/* Output tensors */ run_outputs, run_output_tensors, 1,
		/* Target operations */ NULL, 0,
		/* RunMetadata */ NULL,
		/* Output status */ status);
	OutputDebugStringA("Session finished");
	if (TF_GetCode(status) != TF_OK) {
		snprintf(msgbuf, 100, "ERROR: Unable to run output_op: %s", TF_Message(status));
		OutputDebugStringA(msgbuf);
		return -1;
	}


	void* output_data = TF_TensorData(run_output_tensors[0]);

	snprintf(msgbuf, 100, "TensorByteSize %zi", TF_TensorByteSize(run_output_tensors[0]));
	OutputDebugStringA(msgbuf);
	// todo: tf api to find the beginning of the png?
	OutputDebugStringA("Writing output");
	memcpy(out_intermediate_latent, output_data, 512 * sizeof(float));
	OutputDebugStringA("Cleaning up");

	free((void*)run_outputs);

	free((void*)run_output_tensors);

	free((void*)raw_output_dims);
	return 0;
}

void write_file(const char* data, int bytes, const char* file)
{
	char msgbuf[101];
	FILE* f;
	errno_t err = fopen_s(&f, file, "wb");
	if (err != 0)
	{
		snprintf(msgbuf, 100, "Could not open file for writing %s", file);
		OutputDebugStringA(msgbuf);
		return;
	}
	if (f != NULL)
	{
		fwrite(data, 1, bytes, f);
		fclose(f);
	}
}

TF_Buffer* read_file(const char* file) {
	char msgbuf[101];
	FILE* f;
	errno_t err = fopen_s(&f, file, "rb");
	if (err != 0)
	{
		snprintf(msgbuf, 100, "Could not open file for writing %s", file);
		OutputDebugStringA(msgbuf);
		printf("The file 'data2' was not opened\n");
	}
	if (f == NULL)
	{
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);  // same as rewind(f);

	void* data = malloc(fsize);
	if (data == 0)
	{
		return NULL;
	}
	fread(data, fsize, 1, f);
	fclose(f);

	TF_Buffer* buf = TF_NewBuffer();
	buf->data = data;
	buf->length = fsize;
	buf->data_deallocator = free_buffer;
	return buf;
}