#ifndef NATIVE_TEST_BUILD
#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

void unity_output_char(int c);
void unity_output_flush(void);
void unity_output_complete(void);

#define UNITY_OUTPUT_CHAR(a) unity_output_char(a)
#define UNITY_OUTPUT_FLUSH() unity_output_flush()
#define UNITY_OUTPUT_COMPLETE() unity_output_complete()

#ifdef __cplusplus
}
#endif

#endif
#endif // NATIVE_TEST_BUILD