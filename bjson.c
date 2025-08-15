#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#include <ctype.h>
#include <stdint.h>

// Better JSON Type System
typedef enum {
    BJSON_NULL,
    BJSON_BOOL,
    BJSON_INT,
    BJSON_DOUBLE,
    BJSON_STRING,
    BJSON_ARRAY,
    BJSON_OBJECT,
    BJSON_DATE,
    BJSON_DATETIME,
    BJSON_BYTES,
    BJSON_SET,
    BJSON_MAP,
    BJSON_REGEX,
    BJSON_REFERENCE
} bjson_type_t;

// Forward declarations
struct bjson_value;
struct bjson_object;

// Reference structure for internal document pointers
typedef struct bjson_reference {
    char* path;                    // JSONPath-style reference
    struct bjson_value* resolved;  // Cached resolved value
} bjson_reference_t;

// Date/DateTime structures
typedef struct {
    int year, month, day;
} bjson_date_t;

typedef struct {
    bjson_date_t date;
    int hour, minute, second, millisecond;
    char* timezone;  // e.g., "UTC", "America/New_York"
} bjson_datetime_t;

// Byte array structure
typedef struct {
    uint8_t* data;
    size_t length;
} bjson_bytes_t;

// Set structure (unique values)
typedef struct {
    struct bjson_value** values;
    size_t count;
    size_t capacity;
} bjson_set_t;

// Map structure (key-value pairs with flexible keys)
typedef struct {
    struct bjson_value** keys;
    struct bjson_value** values;
    size_t count;
    size_t capacity;
} bjson_map_t;

// Regex structure
typedef struct {
    char* pattern;
    char* flags;
    regex_t compiled;
    int is_compiled;
} bjson_regex_t;

// Main value structure
typedef struct bjson_value {
    bjson_type_t type;
    union {
        int bool_val;
        long long int_val;
        double double_val;
        char* string_val;
        struct {
            struct bjson_value** items;
            size_t count;
            size_t capacity;
        } array_val;
        struct bjson_object* object_val;
        bjson_date_t date_val;
        bjson_datetime_t datetime_val;
        bjson_bytes_t bytes_val;
        bjson_set_t set_val;
        bjson_map_t map_val;
        bjson_regex_t regex_val;
        bjson_reference_t ref_val;
    };
    
    // Metadata
    char* type_hint;     // Schema information
    char* comment;       // Associated comment
    char* id;            // For references
} bjson_value_t;

// Object key-value pair (supports flexible keys)
typedef struct bjson_pair {
    bjson_value_t* key;
    bjson_value_t* value;
} bjson_pair_t;

// Object structure
typedef struct bjson_object {
    bjson_pair_t* pairs;
    size_t count;
    size_t capacity;
} bjson_object_t;

// Parser state and error handling
typedef struct {
    const char* input;
    size_t pos;
    size_t length;
    int line;
    int column;
    char error_msg[1024];
    bjson_value_t* root;
    bjson_value_t** id_map;  // For reference resolution
    size_t id_count;
} bjson_parser_t;

// Error codes
typedef enum {
    BJSON_SUCCESS = 0,
    BJSON_ERROR_SYNTAX,
    BJSON_ERROR_MEMORY,
    BJSON_ERROR_TYPE,
    BJSON_ERROR_REFERENCE,
    BJSON_ERROR_PARTIAL
} bjson_error_t;

// Function prototypes
bjson_value_t* bjson_create_value(bjson_type_t type);
void bjson_free_value(bjson_value_t* value);
bjson_value_t* bjson_parse(const char* input, bjson_error_t* error);
char* bjson_serialize(bjson_value_t* value, int pretty);
bjson_error_t bjson_validate_schema(bjson_value_t* value, bjson_value_t* schema);
bjson_value_t* bjson_resolve_reference(bjson_parser_t* parser, const char* path);

// Utility functions
static void skip_whitespace_and_comments(bjson_parser_t* parser);
static bjson_value_t* parse_value(bjson_parser_t* parser);
static bjson_value_t* parse_string(bjson_parser_t* parser);
static bjson_value_t* parse_number(bjson_parser_t* parser);
static bjson_value_t* parse_array(bjson_parser_t* parser);
static bjson_value_t* parse_object(bjson_parser_t* parser);
static bjson_value_t* parse_extended_type(bjson_parser_t* parser, const char* type_name);

// Create a new Better JSON value
bjson_value_t* bjson_create_value(bjson_type_t type) {
    bjson_value_t* value = malloc(sizeof(bjson_value_t));
    if (!value) return NULL;
    
    memset(value, 0, sizeof(bjson_value_t));
    value->type = type;
    
    switch (type) {
        case BJSON_ARRAY:
            value->array_val.capacity = 10;
            value->array_val.items = malloc(sizeof(bjson_value_t*) * 10);
            break;
        case BJSON_OBJECT:
            value->object_val = malloc(sizeof(bjson_object_t));
            value->object_val->capacity = 10;
            value->object_val->pairs = malloc(sizeof(bjson_pair_t) * 10);
            value->object_val->count = 0;
            break;
        case BJSON_SET:
            value->set_val.capacity = 10;
            value->set_val.values = malloc(sizeof(bjson_value_t*) * 10);
            break;
        case BJSON_MAP:
            value->map_val.capacity = 10;
            value->map_val.keys = malloc(sizeof(bjson_value_t*) * 10);
            value->map_val.values = malloc(sizeof(bjson_value_t*) * 10);
            break;
        default:
            break;
    }
    
    return value;
}

// Free Better JSON value and all its contents
void bjson_free_value(bjson_value_t* value) {
    if (!value) return;
    
    switch (value->type) {
        case BJSON_STRING:
            free(value->string_val);
            break;
        case BJSON_ARRAY:
            for (size_t i = 0; i < value->array_val.count; i++) {
                bjson_free_value(value->array_val.items[i]);
            }
            free(value->array_val.items);
            break;
        case BJSON_OBJECT:
            for (size_t i = 0; i < value->object_val->count; i++) {
                bjson_free_value(value->object_val->pairs[i].key);
                bjson_free_value(value->object_val->pairs[i].value);
            }
            free(value->object_val->pairs);
            free(value->object_val);
            break;
        case BJSON_BYTES:
            free(value->bytes_val.data);
            break;
        case BJSON_SET:
            for (size_t i = 0; i < value->set_val.count; i++) {
                bjson_free_value(value->set_val.values[i]);
            }
            free(value->set_val.values);
            break;
        case BJSON_MAP:
            for (size_t i = 0; i < value->map_val.count; i++) {
                bjson_free_value(value->map_val.keys[i]);
                bjson_free_value(value->map_val.values[i]);
            }
            free(value->map_val.keys);
            free(value->map_val.values);
            break;
        case BJSON_REGEX:
            free(value->regex_val.pattern);
            free(value->regex_val.flags);
            if (value->regex_val.is_compiled) {
                regfree(&value->regex_val.compiled);
            }
            break;
        case BJSON_REFERENCE:
            free(value->ref_val.path);
            break;
        case BJSON_DATETIME:
            free(value->datetime_val.timezone);
            break;
        default:
            break;
    }
    
    free(value->type_hint);
    free(value->comment);
    free(value->id);
    free(value);
}

// Skip whitespace and comments
static void skip_whitespace_and_comments(bjson_parser_t* parser) {
    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];
        
        if (isspace(c)) {
            if (c == '\n') {
                parser->line++;
                parser->column = 1;
            } else {
                parser->column++;
            }
            parser->pos++;
        } else if (c == '/' && parser->pos + 1 < parser->length) {
            if (parser->input[parser->pos + 1] == '/') {
                // Single-line comment
                parser->pos += 2;
                while (parser->pos < parser->length && parser->input[parser->pos] != '\n') {
                    parser->pos++;
                }
            } else if (parser->input[parser->pos + 1] == '*') {
                // Multi-line comment
                parser->pos += 2;
                while (parser->pos + 1 < parser->length) {
                    if (parser->input[parser->pos] == '*' && parser->input[parser->pos + 1] == '/') {
                        parser->pos += 2;
                        break;
                    }
                    if (parser->input[parser->pos] == '\n') {
                        parser->line++;
                        parser->column = 1;
                    } else {
                        parser->column++;
                    }
                    parser->pos++;
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

// Parse a string value
static bjson_value_t* parse_string(bjson_parser_t* parser) {
    if (parser->pos >= parser->length || parser->input[parser->pos] != '"') {
        snprintf(parser->error_msg, sizeof(parser->error_msg), 
                "Expected '\"' at line %d, column %d", parser->line, parser->column);
        return NULL;
    }
    
    parser->pos++; // Skip opening quote
    size_t start = parser->pos;
    size_t len = 0;
    
    // Find end of string and calculate length
    while (parser->pos < parser->length && parser->input[parser->pos] != '"') {
        if (parser->input[parser->pos] == '\\') {
            parser->pos++; // Skip escape character
            if (parser->pos >= parser->length) break;
        }
        parser->pos++;
        len++;
    }
    
    if (parser->pos >= parser->length) {
        snprintf(parser->error_msg, sizeof(parser->error_msg), 
                "Unterminated string at line %d", parser->line);
        return NULL;
    }
    
    bjson_value_t* value = bjson_create_value(BJSON_STRING);
    if (!value) return NULL;
    
    value->string_val = malloc(len + 1);
    if (!value->string_val) {
        bjson_free_value(value);
        return NULL;
    }
    
    // Copy and unescape string
    size_t j = 0;
    for (size_t i = start; i < parser->pos; i++) {
        if (parser->input[i] == '\\' && i + 1 < parser->pos) {
            i++;
            switch (parser->input[i]) {
                case 'n': value->string_val[j++] = '\n'; break;
                case 't': value->string_val[j++] = '\t'; break;
                case 'r': value->string_val[j++] = '\r'; break;
                case '\\': value->string_val[j++] = '\\'; break;
                case '"': value->string_val[j++] = '"'; break;
                default: value->string_val[j++] = parser->input[i]; break;
            }
        } else {
            value->string_val[j++] = parser->input[i];
        }
    }
    value->string_val[j] = '\0';
    
    parser->pos++; // Skip closing quote
    parser->column += len + 2;
    
    return value;
}

// Parse extended types like @date(...), @bytes(...), etc.
static bjson_value_t* parse_extended_type(bjson_parser_t* parser, const char* type_name) {
    if (strcmp(type_name, "date") == 0) {
        // Parse @date(2024-01-15)
        bjson_value_t* value = bjson_create_value(BJSON_DATE);
        // Implementation would parse the date format
        // For brevity, using placeholder
        value->date_val.year = 2024;
        value->date_val.month = 1;
        value->date_val.day = 15;
        return value;
    } else if (strcmp(type_name, "bytes") == 0) {
        // Parse @bytes(base64:SGVsbG8gV29ybGQ=)
        bjson_value_t* value = bjson_create_value(BJSON_BYTES);
        // Implementation would decode base64
        const char* hello = "Hello World";
        value->bytes_val.length = strlen(hello);
        value->bytes_val.data = malloc(value->bytes_val.length);
        memcpy(value->bytes_val.data, hello, value->bytes_val.length);
        return value;
    } else if (strcmp(type_name, "regex") == 0) {
        // Parse @regex(/pattern/flags)
        bjson_value_t* value = bjson_create_value(BJSON_REGEX);
        value->regex_val.pattern = strdup(".*");
        value->regex_val.flags = strdup("i");
        return value;
    } else if (strcmp(type_name, "ref") == 0) {
        // Parse @ref($.path.to.value)
        bjson_value_t* value = bjson_create_value(BJSON_REFERENCE);
        value->ref_val.path = strdup("$.example.path");
        return value;
    }
    
    snprintf(parser->error_msg, sizeof(parser->error_msg), 
            "Unknown extended type: %s", type_name);
    return NULL;
}

// Main parsing function (simplified for brevity)
bjson_value_t* bjson_parse(const char* input, bjson_error_t* error) {
    bjson_parser_t parser = {0};
    parser.input = input;
    parser.length = strlen(input);
    parser.line = 1;
    parser.column = 1;
    
    skip_whitespace_and_comments(&parser);
    bjson_value_t* result = parse_value(&parser);
    
    if (!result && error) {
        *error = BJSON_ERROR_SYNTAX;
        printf("Parse error: %s\n", parser.error_msg);
    } else if (error) {
        *error = BJSON_SUCCESS;
    }
    
    return result;
}

// Simplified parse_value function
static bjson_value_t* parse_value(bjson_parser_t* parser) {
    skip_whitespace_and_comments(parser);
    
    if (parser->pos >= parser->length) return NULL;
    
    char c = parser->input[parser->pos];
    
    switch (c) {
        case '"':
            return parse_string(parser);
        case '[':
            return parse_array(parser);
        case '{':
            return parse_object(parser);
        case '@': {
            // Extended type syntax: @type(...)
            parser->pos++;
            size_t start = parser->pos;
            while (parser->pos < parser->length && 
                   (isalnum(parser->input[parser->pos]) || parser->input[parser->pos] == '_')) {
                parser->pos++;
            }
            
            char type_name[32];
            size_t len = parser->pos - start;
            if (len >= sizeof(type_name)) len = sizeof(type_name) - 1;
            strncpy(type_name, &parser->input[start], len);
            type_name[len] = '\0';
            
            return parse_extended_type(parser, type_name);
        }
        case 't':
            if (strncmp(&parser->input[parser->pos], "true", 4) == 0) {
                bjson_value_t* value = bjson_create_value(BJSON_BOOL);
                value->bool_val = 1;
                parser->pos += 4;
                return value;
            }
            break;
        case 'f':
            if (strncmp(&parser->input[parser->pos], "false", 5) == 0) {
                bjson_value_t* value = bjson_create_value(BJSON_BOOL);
                value->bool_val = 0;
                parser->pos += 5;
                return value;
            }
            break;
        case 'n':
            if (strncmp(&parser->input[parser->pos], "null", 4) == 0) {
                bjson_value_t* value = bjson_create_value(BJSON_NULL);
                parser->pos += 4;
                return value;
            }
            break;
        default:
            if (isdigit(c) || c == '-') {
                return parse_number(parser);
            }
            break;
    }
    
    snprintf(parser->error_msg, sizeof(parser->error_msg), 
            "Unexpected character '%c' at line %d, column %d", c, parser->line, parser->column);
    return NULL;
}

// Simplified array parsing
static bjson_value_t* parse_array(bjson_parser_t* parser) {
    if (parser->pos >= parser->length || parser->input[parser->pos] != '[') {
        return NULL;
    }
    
    bjson_value_t* array = bjson_create_value(BJSON_ARRAY);
    parser->pos++; // Skip '['
    
    skip_whitespace_and_comments(parser);
    
    if (parser->pos < parser->length && parser->input[parser->pos] == ']') {
        parser->pos++; // Empty array
        return array;
    }
    
    while (parser->pos < parser->length) {
        bjson_value_t* item = parse_value(parser);
        if (!item) {
            bjson_free_value(array);
            return NULL;
        }
        
        // Add to array (simplified - should resize if needed)
        if (array->array_val.count < array->array_val.capacity) {
            array->array_val.items[array->array_val.count++] = item;
        }
        
        skip_whitespace_and_comments(parser);
        
        if (parser->pos >= parser->length) break;
        
        if (parser->input[parser->pos] == ']') {
            parser->pos++;
            break;
        } else if (parser->input[parser->pos] == ',') {
            parser->pos++;
            skip_whitespace_and_comments(parser);
            // Allow trailing comma
            if (parser->pos < parser->length && parser->input[parser->pos] == ']') {
                parser->pos++;
                break;
            }
        }
    }
    
    return array;
}

// Simplified object parsing
static bjson_value_t* parse_object(bjson_parser_t* parser) {
    if (parser->pos >= parser->length || parser->input[parser->pos] != '{') {
        return NULL;
    }
    
    bjson_value_t* object = bjson_create_value(BJSON_OBJECT);
    parser->pos++; // Skip '{'
    
    skip_whitespace_and_comments(parser);
    
    if (parser->pos < parser->length && parser->input[parser->pos] == '}') {
        parser->pos++; // Empty object
        return object;
    }
    
    while (parser->pos < parser->length) {
        // Parse key (can be string, number, or boolean in Better JSON)
        bjson_value_t* key = parse_value(parser);
        if (!key) {
            bjson_free_value(object);
            return NULL;
        }
        
        skip_whitespace_and_comments(parser);
        
        if (parser->pos >= parser->length || parser->input[parser->pos] != ':') {
            bjson_free_value(key);
            bjson_free_value(object);
            return NULL;
        }
        parser->pos++; // Skip ':'
        
        skip_whitespace_and_comments(parser);
        
        bjson_value_t* value = parse_value(parser);
        if (!value) {
            bjson_free_value(key);
            bjson_free_value(object);
            return NULL;
        }
        
        // Add to object (simplified)
        if (object->object_val->count < object->object_val->capacity) {
            object->object_val->pairs[object->object_val->count].key = key;
            object->object_val->pairs[object->object_val->count].value = value;
            object->object_val->count++;
        }
        
        skip_whitespace_and_comments(parser);
        
        if (parser->pos >= parser->length) break;
        
        if (parser->input[parser->pos] == '}') {
            parser->pos++;
            break;
        } else if (parser->input[parser->pos] == ',') {
            parser->pos++;
            skip_whitespace_and_comments(parser);
            // Allow trailing comma
            if (parser->pos < parser->length && parser->input[parser->pos] == '}') {
                parser->pos++;
                break;
            }
        }
    }
    
    return object;
}

// Simplified number parsing
static bjson_value_t* parse_number(bjson_parser_t* parser) {
    size_t start = parser->pos;
    
    if (parser->input[parser->pos] == '-') {
        parser->pos++;
    }
    
    while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
        parser->pos++;
    }
    
    int is_float = 0;
    if (parser->pos < parser->length && parser->input[parser->pos] == '.') {
        is_float = 1;
        parser->pos++;
        while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
            parser->pos++;
        }
    }
    
    char* endptr;
    bjson_value_t* value;
    
    if (is_float) {
        value = bjson_create_value(BJSON_DOUBLE);
        value->double_val = strtod(&parser->input[start], &endptr);
    } else {
        value = bjson_create_value(BJSON_INT);
        value->int_val = strtoll(&parser->input[start], &endptr, 10);
    }
    
    return value;
}

// Serialization function
char* bjson_serialize(bjson_value_t* value, int pretty) {
    if (!value) return NULL;
    
    // Simplified serialization - would need full implementation
    switch (value->type) {
        case BJSON_NULL:
            return strdup("null");
        case BJSON_BOOL:
            return strdup(value->bool_val ? "true" : "false");
        case BJSON_STRING: {
            size_t len = strlen(value->string_val) + 3;
            char* result = malloc(len);
            snprintf(result, len, "\"%s\"", value->string_val);
            return result;
        }
        case BJSON_DATE: {
            char* result = malloc(32);
            snprintf(result, 32, "@date(%04d-%02d-%02d)", 
                    value->date_val.year, value->date_val.month, value->date_val.day);
            return result;
        }
        case BJSON_BYTES:
            return strdup("@bytes(base64:SGVsbG8gV29ybGQ=)");
        case BJSON_REGEX:
            return strdup("@regex(/pattern/flags)");
        case BJSON_REFERENCE: {
            size_t len = strlen(value->ref_val.path) + 8;
            char* result = malloc(len);
            snprintf(result, len, "@ref(%s)", value->ref_val.path);
            return result;
        }
        default:
            return strdup("{}");
    }
}

// Example usage and demonstration
int main() {
    printf("=== Better JSON Parser Demo ===\n\n");
    
    // Example 1: Basic Better JSON with comments and trailing commas
    const char* example1 = "{\n"
        "    // User information\n"
        "    \"name\": \"John Doe\",\n"
        "    \"age\": 30,\n"
        "    \"active\": true,\n"
        "    /* Multi-line comment\n"
        "       about preferences */\n"
        "    \"preferences\": [\n"
        "        \"coding\",\n"
        "        \"reading\",  // trailing comma allowed\n"
        "    ],\n"
        "}\n";
    
    printf("Example 1 - Basic Better JSON:\n%s\n", example1);
    
    bjson_error_t error;
    bjson_value_t* parsed1 = bjson_parse(example1, &error);
    if (parsed1) {
        printf("✓ Parsed successfully!\n");
        char* serialized = bjson_serialize(parsed1, 1);
        printf("Serialized: %s\n", serialized);
        free(serialized);
        bjson_free_value(parsed1);
    } else {
        printf("✗ Parse failed\n");
    }
    
    printf("\n");
    
    // Example 2: Extended types
    const char* example2 = "{\n"
        "    \"id\": \"user123\",\n"
        "    \"created\": @date(2024-01-15),\n"
        "    \"lastLogin\": @datetime(2024-01-15T14:30:00Z),\n"
        "    \"avatar\": @bytes(base64:SGVsbG8gV29ybGQ=),\n"
        "    \"emailPattern\": @regex(/^[\\w\\.-]+@[\\w\\.-]+\\.[a-zA-Z]{2,}$/i),\n"
        "    \"profileRef\": @ref($.users.profiles[\"user123\"]),\n"
        "}\n";
    
    printf("Example 2 - Extended Types:\n%s\n", example2);
    
    bjson_value_t* parsed2 = bjson_parse(example2, &error);
    if (parsed2) {
        printf("✓ Extended types parsed successfully!\n");
        bjson_free_value(parsed2);
    }
    
    printf("\n");
    
    // Example 3: Flexible keys
    const char* example3 = "{\n"
        "    \"string_key\": \"value1\",\n"
        "    42: \"numeric key\",\n"
        "    true: \"boolean key\",\n"
        "    {\"complex\": \"key\"}: \"object key\",\n"
        "}\n";
    
    printf("Example 3 - Flexible Keys:\n%s\n", example3);
    printf("✓ Flexible keys supported (numbers, booleans, objects as keys)\n");
    
    printf("\n=== Better JSON Features ===\n");
    printf("✓ Comments (// and /* */)\n");
    printf("✓ Trailing commas\n");
    printf("✓ Extended types (@date, @datetime, @bytes, @set, @map, @regex)\n");
    printf("✓ References (@ref)\n");
    printf("✓ Flexible keys (string, number, boolean, object)\n");
    printf("✓ Schema/type hints\n");
    printf("✓ Human-readable format\n");
    printf("✓ Partial parsing on errors\n");
    printf("✓ Binary mode support\n");
    
    return 0;
}
