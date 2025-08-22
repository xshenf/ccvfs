#include "unit_test_framework.h"

// Test version of hex parsing function
static int hex_string_to_bytes_test(const char *hexStr, unsigned char *output, int maxBytes) {
    if (!hexStr || !output) {
        return -1;
    }
    
    int hexLen = (int)strlen(hexStr);
    if (hexLen % 2 != 0) {
        return -1;
    }
    
    int byteLen = hexLen / 2;
    if (byteLen > maxBytes) {
        return -1;
    }
    
    for (int i = 0; i < byteLen; i++) {
        char hex[3] = {hexStr[i*2], hexStr[i*2+1], '\0'};
        char *endptr;
        unsigned long val = strtoul(hex, &endptr, 16);
        
        if (*endptr != '\0' || val > 255) {
            return -1;
        }
        
        output[i] = (unsigned char)val;
    }
    
    return byteLen;
}

static int test_basic_hex_parsing(void) {
    unsigned char output[32];
    int result;
    
    result = hex_string_to_bytes_test("48656c6c6f", output, sizeof(output));
    UT_ASSERT_EQUAL(5, result, "Parse 'Hello' hex string length");
    UT_ASSERT_EQUAL('H', output[0], "First byte should be 'H'");
    UT_ASSERT_EQUAL('e', output[1], "Second byte should be 'e'");
    UT_ASSERT_EQUAL('l', output[2], "Third byte should be 'l'");
    UT_ASSERT_EQUAL('l', output[3], "Fourth byte should be 'l'");
    UT_ASSERT_EQUAL('o', output[4], "Fifth byte should be 'o'");
    
    return 1;
}

static int test_error_cases(void) {
    unsigned char output[16];
    int result;
    
    // Test odd length string
    result = hex_string_to_bytes_test("abc", output, sizeof(output));
    UT_ASSERT_EQUAL(-1, result, "Odd length string should fail");
    
    // Test invalid characters
    result = hex_string_to_bytes_test("abcg", output, sizeof(output));
    UT_ASSERT_EQUAL(-1, result, "Invalid hex character should fail");
    
    // Test NULL input
    result = hex_string_to_bytes_test(NULL, output, sizeof(output));
    UT_ASSERT_EQUAL(-1, result, "NULL input should fail");
    
    return 1;
}

int test_hex_key_parsing(UnitTestResult* result) {
    UT_BEGIN_TEST("Hexadecimal Key Parsing");
    
    if (!test_basic_hex_parsing()) return 0;
    if (!test_error_cases()) return 0;
    
    UT_END_TEST();
    return 1;
}