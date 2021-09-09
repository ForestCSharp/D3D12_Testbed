#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

const uint32_t GLTF_MAGIC_NUMBER      = 0x46546C67;
const uint32_t GLTF_CHUNK_TYPE_JSON   = 0x4E4F534A;
const uint32_t GLTF_CHUNK_TYPE_BUFFER = 0x004E4942;

const char* STRING_TRUE = "true";
const char* STRING_FALSE = "false";

typedef struct JsonObject {
    uint32_t count;
    struct JsonKeyValuePair* key_value_pairs;
} JsonObject;

typedef struct JsonArray {
    uint32_t count;
    struct JsonValue* values;
} JsonArray;

typedef enum JsonValueType {
    JSON_VALUE_TYPE_STRING,
    JSON_VALUE_TYPE_NUMBER,
    JSON_VALUE_TYPE_OBJECT,
    JSON_VALUE_TYPE_BOOLEAN,
    JSON_VALUE_TYPE_ARRAY,
} JsonValueType;

typedef struct JsonValue {
    JsonValueType type;
    union {
        char* string;
        float number;
        JsonObject object;
        bool boolean;
        JsonArray array;
    } data;
} JsonValue;

typedef struct JsonKeyValuePair {
    char* key;
    JsonValue value;
} JsonKeyValuePair;

//returns true if char c exists in string str
bool is_char_in_string(char c, const char* str) {
    for (uint32_t i = 0; i < strlen(str); ++i) {
        if (c == str[i]) {
            return true;
        }
    }
    return false;
}

//can modify p_string, trims p_string of leading chars_to_trim
int trim_characters(char** p_string, const char* chars_to_trim) {
    uint32_t i = 0;
    for (; i < strlen(*p_string); ++i) {
        if (!is_char_in_string(*p_string[i], chars_to_trim)) {
            *p_string = *p_string + i;
            break;
        }
    }
    return i;
}

//can modify p_string, trims whitespace characters
int trim_whitespace(char** p_string) {
    return trim_characters(p_string, " \t\v\n\r\f");
}

//can modify p_string, trims whitespace and attempts to consume char c
bool consume(char c, char** p_string) {
    int chars_trimmed = trim_whitespace(p_string);
    if (c == *p_string[0]) {
        *p_string = *p_string + 1;
        return true;
    }
    return false;
}

//Finds next occurrence of c in in_string
int find_next(char c, char* in_string) {
    for (uint32_t i = 0; i < strlen(in_string); ++i) {
        if (c == in_string[i]) {
            return i;
        }
    }
    return -1;
}

//returns true if the next n-chars of in_string match identifier (n == strlen(identifier))
bool string_check(const char* in_string, const char* identifier) {
    for (int i = 0; i < strlen(identifier); ++i) {
        if (i >= strlen(in_string) || in_string[i] != identifier[i]) {
            return false;
        }
    }
    return true;
}

//can modify p_string, allocates and returns a new string
char* parse_string(char** p_string) {
    char** old_string = p_string;
    bool found_leading_quote = consume('\"', p_string);
    if (found_leading_quote) {
        int key_length = find_next('\"', *p_string);
        //FIXME: check for escape char before ending quote
        if (key_length >= 0) {
            char* parsed_string = (char*) calloc(key_length + 1, 1);
            memcpy(parsed_string, *p_string, key_length);
            *p_string = *p_string + key_length + 1;
            return parsed_string;
        }
    }
    return NULL;
}

//Allocations: Parsed Strings, Arrays, JsonObject KeyValue pairs

bool parse_json_object(char** json_string, JsonObject* out_json_object);

bool parse_json_value(char** json_string, JsonValue* out_value) {
    char* current_position = *json_string;

    trim_whitespace(&current_position);
    if (current_position[0] == '{') {
        out_value->type = JSON_VALUE_TYPE_OBJECT;
        parse_json_object(&current_position, &out_value->data.object);
    }
    else if (current_position[0] == '"') {
        out_value->type = JSON_VALUE_TYPE_STRING;
        out_value->data.string = parse_string(&current_position);
    }
    else if (isdigit(current_position[0]) || current_position[0] == '-') {
        char* end_ptr = NULL;
        out_value->type = JSON_VALUE_TYPE_NUMBER;
        out_value->data.number = strtof(current_position, &end_ptr);
        if (current_position != end_ptr) {
            current_position = end_ptr;
        }
    }
    else if (current_position[0] == '[') {
        out_value->type = JSON_VALUE_TYPE_ARRAY;
        out_value->data.array.count = 0;
        out_value->data.array.values = NULL;
        consume('[', &current_position);
        do {
            out_value->data.array.count += 1;
            out_value->data.array.values = (JsonValue*) realloc(out_value->data.array.values, sizeof(JsonValue) * out_value->data.array.count);
            JsonValue* array_value = &out_value->data.array.values[out_value->data.array.count - 1];
            memset(array_value, 0, sizeof(JsonValue));
            if (!parse_json_value(&current_position, array_value)) { return false; }

        } while(consume(',', &current_position));
        consume(']', &current_position);
    }
    else if (string_check(current_position, STRING_TRUE)) {
        out_value->type = JSON_VALUE_TYPE_BOOLEAN;
        out_value->data.boolean = true;
        current_position += strlen(STRING_TRUE);
    }
    else if (string_check(current_position, STRING_FALSE)) {
        out_value->type = JSON_VALUE_TYPE_BOOLEAN;
        out_value->data.boolean = false;
        current_position += strlen(STRING_FALSE);
    }

    *json_string = current_position;
    return true;
}

//Modifies json_string (necessary for recursion)
bool parse_json_object(char** json_string, JsonObject* out_json_object) {
    char* current_position = *json_string;
    //1. Consume leading whitespace and '{'
    if (!consume('{', &current_position)) { return false; }

    //2. Iterate over key/value pairs
    out_json_object->count = 0;
    out_json_object->key_value_pairs = NULL;
    do {
        out_json_object->count += 1;
        out_json_object->key_value_pairs = (JsonKeyValuePair*) realloc(out_json_object->key_value_pairs, sizeof(JsonKeyValuePair) * out_json_object->count);
        JsonKeyValuePair* key_value = &out_json_object->key_value_pairs[out_json_object->count - 1];
        memset(key_value, 0, sizeof(JsonKeyValuePair));

        //Key (string)
        key_value->key = parse_string(&current_position);
        if (key_value->key == NULL) { return false; }
        
        if (!consume(':', &current_position)) { return false; }
        if (!parse_json_value(&current_position, &key_value->value)) { return false; }

    } while (consume(',', &current_position));

    //Consume final closing bracket
    if (!consume('}', &current_position)) { return false; }

    *json_string = current_position;
    return true;
}

bool json_value_as_float(const JsonValue* value, float* out_float) {
    if (value && value->type == JSON_VALUE_TYPE_NUMBER && out_float) {
        *out_float = value->data.number;
        return true;
    }
    return false;
}

bool json_value_as_int32(const JsonValue* value, int32_t* out_int) {
    if (value && value->type == JSON_VALUE_TYPE_NUMBER && out_int) {
        *out_int = (int32_t) value->data.number;
        return true;
    }
    return false;
}

bool json_value_as_uint32(const JsonValue* value, uint32_t* out_int) {
    if (value && value->type == JSON_VALUE_TYPE_NUMBER && out_int) {
        *out_int = (uint32_t) value->data.number;
        return true;
    }
    return false;
}

bool json_value_as_bool(const JsonValue* value, bool* out_bool) {
    if (value && value->type == JSON_VALUE_TYPE_BOOLEAN && out_bool) {
        *out_bool = value->data.boolean;
        return true;
    }
    return false;
}

bool json_value_as_string(const JsonValue* value, const char** out_string) {
    if (value && value->type == JSON_VALUE_TYPE_STRING && out_string) {
        *out_string = value->data.string;
        return true;
    }
    return false;
}

bool json_value_as_array(const JsonValue* value, const JsonArray** out_array) {
    if (value && value->type == JSON_VALUE_TYPE_ARRAY) {
        *out_array = &value->data.array;
        return true;
    }
    return false;
}

bool json_value_as_object(const JsonValue* value, const JsonObject** out_object) {
    if (value && value->type == JSON_VALUE_TYPE_OBJECT) {
        *out_object = &value->data.object;
        return true;
    }
    return false;
}

const JsonValue* json_object_get_value(const JsonObject* object, const char* key) {
    for (uint32_t i = 0; i < object->count; ++i) {
        JsonKeyValuePair* key_value = &object->key_value_pairs[i];
        if (strcmp(key_value->key, key) == 0) {
            return &key_value->value;
        }
    }
    return NULL;
}

const JsonValue* json_array_get_value(const JsonArray* array, uint32_t index) {
    if (index < array->count) {
        return &array->values[index];
    }
    return NULL;
}

//Convenience Functions Below
// the JsonObject and JsonArray getters return pointers
// TODO: the other value getters return true on success and set the output via an argument

const JsonObject* json_object_get_object(const JsonObject* object, const char* key) {
    const JsonObject* out_object = NULL;
    json_value_as_object(json_object_get_value(object, key), &out_object);
    return out_object;
}

const JsonArray* json_object_get_array(const JsonObject* object, const char* key) {
    const JsonArray* out_array = NULL;
    json_value_as_array(json_object_get_value(object, key), &out_array);
    return out_array;
}

const JsonObject* json_array_get_object(const JsonArray* array, uint32_t index) {
    const JsonObject* out_object = NULL;
    json_value_as_object(json_array_get_value(array, index), &out_object);
    return out_object;
}

const JsonArray* json_array_get_array(const JsonArray* array, uint32_t index) {
    const JsonArray* out_array = NULL;
    json_value_as_array(json_array_get_value(array, index), &out_array);
    return out_array;
}

void free_json_object(JsonObject* in_object);

void free_json_value(JsonValue* in_value) {
    switch(in_value->type) {
        case JSON_VALUE_TYPE_OBJECT:
            free_json_object(&in_value->data.object);
            break;
        case JSON_VALUE_TYPE_ARRAY:
            for (uint32_t i = 0; i < in_value->data.array.count; ++i) {
                free_json_value(&in_value->data.array.values[i]);
            }
            free(in_value->data.array.values);
            break;
        case JSON_VALUE_TYPE_STRING:
            free(in_value->data.string);
            break;
        default: 
            break;
    }
}

void free_json_object(JsonObject* in_object) {
    for (uint32_t i = 0; i < in_object->count; ++i) {
        JsonKeyValuePair* key_value = &in_object->key_value_pairs[i];
        free_json_value(&key_value->value);
    }
    free(in_object->key_value_pairs);
}

static inline void indent(FILE* out_file, int n)
{
    for (int i = 0; i < n; i++) {
        fprintf(out_file, "\t");
    }
}

static inline void indent_printf(FILE* out_file, int n, const char* format, ...) {
    indent(out_file, n);
    va_list args;
    va_start(args, format);
    vfprintf(out_file, format, args);
    va_end(args);
}

//TODO: Option to add/not add newlines

void print_json_object(JsonObject* in_object, int depth, FILE* out_file);

void print_json_value(JsonValue* in_value, int depth, bool leading_indent, FILE* out_file) {
    
    if (leading_indent) {
        indent(out_file, depth);
    }
    
    switch(in_value->type) {
            case JSON_VALUE_TYPE_OBJECT:
                print_json_object(&in_value->data.object, depth, out_file);
                break;
            case JSON_VALUE_TYPE_NUMBER:
                fprintf(out_file, "%f", in_value->data.number);
                break;
            case JSON_VALUE_TYPE_BOOLEAN:
                fprintf(out_file, "%s", in_value->data.boolean ? "true" : "false");
                break;
            case JSON_VALUE_TYPE_ARRAY:
                fprintf(out_file, "[");
                for (uint32_t i = 0; i < in_value->data.array.count; ++i) {
                    fprintf(out_file, "\n");
                    print_json_value(&in_value->data.array.values[i], depth + 1, true, out_file);
                    if (i < in_value->data.array.count - 1) {
                        fprintf(out_file, ", ");
                    }
                }
                fprintf(out_file, "\n");
                indent_printf(out_file, depth, "]");
                break;
            case JSON_VALUE_TYPE_STRING:
                fprintf(out_file, "\"%s\"", in_value->data.string);
                break;
            default: 
                break;
        }
}

void print_json_object(JsonObject* in_object, int depth, FILE* out_file) {

    fprintf(out_file, "{\n");
    
    for (uint32_t i = 0; i < in_object->count; ++i) {
        JsonKeyValuePair* key_value = &in_object->key_value_pairs[i];
        indent_printf(out_file, depth + 1, "\"%s\" : ", key_value->key);

        print_json_value(&key_value->value, depth + 1, false, out_file);
        if (i < in_object->count - 1) {
            fprintf(out_file, ",");
        }
        fprintf(out_file, "\n");
    }
    indent_printf(out_file, depth, "}");

    if (depth == 0) { fprintf(out_file, "\n"); }
}

typedef struct GltfBuffer {
    uint32_t byte_length;
    uint8_t* data;
} GltfBuffer;

typedef struct GltfBufferView {
    uint32_t byte_length;
    uint32_t byte_offset;
    GltfBuffer* buffer;
} GltfBufferView;

typedef enum GltfComponentType {
    GLTF_COMPONENT_TYPE_BYTE = 5120,
    GLTF_COMPONENT_TYPE_UNSIGNED_BYTE = 5121,
    GLTF_COMPONENT_TYPE_SHORT = 5122,
    GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
    GLTF_COMPONENT_TYPE_UNSIGNED_INT = 5125,
    GLTF_COMPONENT_TYPE_FLOAT = 5126,
} GltfComponentType;

uint32_t gltf_component_type_size(GltfComponentType type) {
    switch (type) {
        case GLTF_COMPONENT_TYPE_BYTE:
        case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
        case GLTF_COMPONENT_TYPE_SHORT:
        case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
        case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case GLTF_COMPONENT_TYPE_FLOAT:
            return 4;
        default:
            return 0;
    }
}

typedef enum GltfAccessorType {
    GLTF_ACCESSOR_TYPE_SCALAR,
    GLTF_ACCESSOR_TYPE_VEC2,
    GLTF_ACCESSOR_TYPE_VEC3,
    GLTF_ACCESSOR_TYPE_VEC4,
    GLTF_ACCESSOR_TYPE_MAT2,
    GLTF_ACCESSOR_TYPE_MAT3,
    GLTF_ACCESSOR_TYPE_MAT4,
} GltfAccessorType;

uint32_t gltf_accessor_type_size(GltfAccessorType type) {
    switch (type) {
        case GLTF_ACCESSOR_TYPE_SCALAR:
            return 1;
        case GLTF_ACCESSOR_TYPE_VEC2:
            return 2;
        case GLTF_ACCESSOR_TYPE_VEC3:
            return 3;
        case GLTF_ACCESSOR_TYPE_VEC4:
        case GLTF_ACCESSOR_TYPE_MAT2:
            return 4;
        case GLTF_ACCESSOR_TYPE_MAT3:
            return 9;
        case GLTF_ACCESSOR_TYPE_MAT4:
            return 16;
        default:
            return 0;
    }
}

#define CHECK_ACCESSOR_TYPE(str, suffix) if (strcmp(str, #suffix) == 0) { return GLTF_ACCESSOR_TYPE_##suffix; }

GltfAccessorType str_to_gltf_accessor_type(const char* str) {
    CHECK_ACCESSOR_TYPE(str, SCALAR);
    CHECK_ACCESSOR_TYPE(str, VEC2);
    CHECK_ACCESSOR_TYPE(str, VEC3);
    CHECK_ACCESSOR_TYPE(str, VEC4);
    CHECK_ACCESSOR_TYPE(str, MAT2);
    CHECK_ACCESSOR_TYPE(str, MAT3);
    CHECK_ACCESSOR_TYPE(str, MAT4);
    return (GltfAccessorType) -1; //Should Never Hit
}

//Stores info for Accessor/View/Buffer
typedef struct GltfAccessor {
    GltfComponentType component_type;
    GltfAccessorType accessor_type;
    uint32_t count;
    uint32_t byte_offset;
    GltfBufferView* buffer_view;
} GltfAccessor;

uint32_t gltf_accessor_get_initial_offset(GltfAccessor* accessor) {
    return accessor->byte_offset + accessor->buffer_view->byte_offset;
}

uint32_t gltf_accessor_get_stride(GltfAccessor* accessor) {
    return gltf_accessor_type_size(accessor->accessor_type) 
        * gltf_component_type_size(accessor->component_type);
}

typedef struct GltfPrimitive {
    GltfAccessor* positions;
    GltfAccessor* normals;
    //TODO: TangentAccessor
    GltfAccessor* texcoord0;
    //TODO: TexCoords Accessors (0,1)
    //TODO: Color Accessor
    //TODO: Joints, Weights Acessors
    GltfAccessor* indices;
    uint32_t material;
} GltfPrimitive;

typedef struct GltfMesh {
    const char* name;
    uint32_t num_primitives;
    GltfPrimitive* primitives;
} GltfMesh;

typedef struct GltfImage {
    GltfBufferView* buffer_view;
    //TODO: MIME Type
} GltfImage;

//TODO: Samplers

typedef struct GltfTexture {
    // GltfSampler* sampler;
    GltfImage* image;
} GltfTexture;

typedef struct GltfPbrMetallicRoughness
{
    float base_color_factor[4]; //default to [1,1,1,1]
    GltfTexture* base_color_texture;
    uint32_t base_color_tex_coord;
    float metallic_factor;  //default to 1.0
    float roughness_factor; //default to 1.0
    GltfTexture* metallic_roughness_texture;
    uint32_t metallic_roughness_tex_coord;
} GltfPbrMetallicRoughness;

typedef struct GltfMaterial {
    bool double_sided;
    //TODO: name
    GltfPbrMetallicRoughness pbr_metallic_roughness;
    //TODO: normal texture
    //TODO: emissive factor
    //TODO: emissive texture
    //TODO: occlusion texture
} GltfMaterial;

typedef struct GltfAsset {
    JsonObject      json;
    uint32_t        num_buffers;
    GltfBuffer*     buffers;
    uint32_t        num_buffer_views;
    GltfBufferView* buffer_views;
    uint32_t        num_accessors;
    GltfAccessor*   accessors;
    uint32_t        num_meshes;
    GltfMesh*       meshes;
    uint32_t        num_images;
    GltfImage*      images;
    uint32_t        num_textures;
    GltfTexture*    textures;
    uint32_t        num_materials;
    GltfMaterial*   materials;
} GltfAsset;

FILE* open_binary_file(const char* filename)
{
    #ifdef _MSC_VER
    #pragma warning(disable : 4996)
    #endif
    return fopen(filename, "rb");
    #ifdef _MSC_VER
    #pragma warning(default : 4996)
    #endif
}

bool gltf_load_asset(const char* filename, GltfAsset* out_asset) {

    //TODO: check extension, add functions for GLB and GLTF (only GLB is currently supported)

    FILE* file = open_binary_file(filename);

    if (file && out_asset)
    {
        // HEADER
        uint32_t magic, version, length;
        if (fread(&magic, 4, 1, file) == 1 && magic == GLTF_MAGIC_NUMBER) {
            printf("We have GLTF Binary!\n");
        }
        if (fread(&version, 4, 1, file) == 1) {
            printf("Version: %i\n", version);
        }
        if (fread(&length, 4, 1, file) == 1) {
            printf("Length: %i\n", length);
        }

        // JSON
        uint32_t json_length, json_type;
        if (fread(&json_length, 4, 1, file) == 1) {
            printf("Json Length: %i\n", json_length);
        }
        if (fread(&json_type, 4, 1, file) == 1 && json_type == GLTF_CHUNK_TYPE_JSON) {
            printf("Found Json Chunk\n");
        }

        char* json_string = (char*) malloc(json_length + 1);
        json_string[json_length] = 0; //Null-terminate string

        if (fread(json_string, json_length, 1, file) != 0) {
            printf("json data: %s\n", json_string);
            char* modified_json_string = json_string;
            parse_json_object(&modified_json_string, &out_asset->json);
            //TODO: Error checking, make sure to free json string if we early-exit
        }

        free(json_string);

        //BUFFERS
        {
            //Note: .glb files only use only 1 buffer chunk
            
            out_asset->num_buffers = 0;
            out_asset->buffers = NULL;

            uint32_t buffer_length;
            while (fread(&buffer_length, 4, 1, file) == 1) {
                // Increment num_buffers and realloc
                out_asset->num_buffers++;
                out_asset->buffers = (GltfBuffer*) realloc(out_asset->buffers, sizeof(GltfBuffer) * out_asset->num_buffers);

                printf("Buffer Length: %i\n", buffer_length);

                uint32_t buffer_type = 0;
                if (fread(&buffer_type, 4, 1, file) != 1 || buffer_type != GLTF_CHUNK_TYPE_BUFFER) {
                    return false;
                }

                uint8_t* buffer_data = (uint8_t*) malloc(buffer_length);
                if (fread(buffer_data, buffer_length, 1, file) == 1) {
                    out_asset->buffers[out_asset->num_buffers - 1].byte_length = buffer_length;
                    out_asset->buffers[out_asset->num_buffers - 1].data = buffer_data;
                } else {
                    return false;
                }
            }
        }

        //BUFFER VIEWS
        {
            const JsonArray* json_buffer_views = json_object_get_array(&out_asset->json, "bufferViews");
            if (!json_buffer_views) {
                return false;
            }

            out_asset->num_buffer_views = json_buffer_views->count;
            out_asset->buffer_views = (GltfBufferView*) calloc(sizeof(GltfBufferView), out_asset->num_buffer_views);

            for (uint32_t i = 0; i < out_asset->num_buffer_views; ++i) {
                GltfBufferView* buffer_view = &out_asset->buffer_views[i];
                const JsonObject* json_buffer_view = json_array_get_object(json_buffer_views, i);
            
                uint32_t buffer_index = 0;
                if (json_value_as_uint32(json_object_get_value(json_buffer_view, "buffer"), &buffer_index) && buffer_index < out_asset->num_buffers) {
                    buffer_view->buffer = &out_asset->buffers[buffer_index];
                } else {
                    return false;
                }

                if (!json_value_as_uint32(json_object_get_value(json_buffer_view, "byteLength"), &buffer_view->byte_length)) {
                    return false;
                }

                json_value_as_uint32(json_object_get_value(json_buffer_view, "byteOffset"), &buffer_view->byte_offset);
            }
        }

        //ACCESSORS
        {
            const JsonArray* json_accessors = json_object_get_array(&out_asset->json, "accessors");
            if (!json_accessors) {
                return false;
            }

            out_asset->num_accessors = json_accessors->count;
            out_asset->accessors = (GltfAccessor*) calloc(sizeof(GltfAccessor), out_asset->num_accessors);

            for (uint32_t i = 0; i < out_asset->num_accessors; ++i) {
                GltfAccessor* accessor = &out_asset->accessors[i];
                const JsonObject* json_accessor = json_array_get_object(json_accessors, i);
            
                uint32_t buffer_view_index = 0;
                if (json_value_as_uint32(json_object_get_value(json_accessor, "bufferView"), &buffer_view_index) && buffer_view_index < out_asset->num_buffer_views) {
                    accessor->buffer_view = &out_asset->buffer_views[buffer_view_index];
                } else {
                    return false;
                }

                if (!json_value_as_uint32(json_object_get_value(json_accessor, "componentType"), (uint32_t*) &accessor->component_type)) {
                    return false;
                }
                if (!json_value_as_uint32(json_object_get_value(json_accessor, "count"), &accessor->count)) {
                    return false;
                }
                const char* type_string = NULL;
                if (json_value_as_string(json_object_get_value(json_accessor, "type"), &type_string)) {
                    accessor->accessor_type = str_to_gltf_accessor_type(type_string);
                }

                json_value_as_uint32(json_object_get_value(json_accessor, "byteOffset"), &accessor->byte_offset);
            }
        }

        //MESHES
        {
            const JsonArray* json_meshes = json_object_get_array(&out_asset->json, "meshes");
            if (!json_meshes) {
                return false;
            }

            out_asset->num_meshes = json_meshes->count;
            out_asset->meshes = (GltfMesh*) calloc(sizeof(GltfMesh), out_asset->num_meshes);

            for (uint32_t i = 0; i < out_asset->num_meshes; ++i) {
                GltfMesh* mesh = &out_asset->meshes[i];
                const JsonObject* json_mesh = json_array_get_object(json_meshes, i);
                const JsonArray* json_primitives = json_object_get_array(json_mesh, "primitives");
            
                if (!json_primitives) {
                    return false;
                }

                mesh->num_primitives = json_primitives->count;
                mesh->primitives = (GltfPrimitive*) calloc(sizeof(GltfPrimitive), mesh->num_primitives);

                for (uint32_t j = 0; j < mesh->num_primitives; ++j) {
                    GltfPrimitive* primitive = &mesh->primitives[j];
                    const JsonObject* json_primitive = json_array_get_object(json_primitives, j);
                    const JsonObject* json_attributes = json_object_get_object(json_primitive, "attributes");

                    //TODO: Primitive Topology (Triangle (4) is default, but check for others)

                    uint32_t positions_index = 0;
                    if (json_value_as_uint32(json_object_get_value(json_attributes, "POSITION"), &positions_index) && positions_index < out_asset->num_accessors) {
                        primitive->positions = &out_asset->accessors[positions_index];
                    }
 
                    uint32_t normals_index = 0;
                    if (json_value_as_uint32(json_object_get_value(json_attributes, "NORMAL"), &normals_index) && normals_index < out_asset->num_accessors) {
                        primitive->normals = &out_asset->accessors[normals_index];
                    }

                    uint32_t texcoord0_index = 0;
                    if (json_value_as_uint32(json_object_get_value(json_attributes, "TEXCOORD_0"), &texcoord0_index) && texcoord0_index < out_asset->num_accessors) {
                        primitive->texcoord0 = &out_asset->accessors[texcoord0_index];
                    }

                    uint32_t indices_index = 0;
                    if (json_value_as_uint32(json_object_get_value(json_primitive, "indices"), &indices_index) && indices_index < out_asset->num_accessors) {
                        primitive->indices = &out_asset->accessors[indices_index];
                    }

                    uint32_t material_index = 0;
                    if (json_value_as_uint32(json_object_get_value(json_primitive, "material_index"), &material_index)) {
                        primitive->material = material_index;
                    }
                }
            }
        }

        //TODO: NODES

        //TODO: SCENES

        //TODO: Skinning

        //IMAGES
        {
            const JsonArray* json_images = json_object_get_array(&out_asset->json, "images");
            if (json_images) {
                out_asset->num_images = json_images->count;
                out_asset->images = (GltfImage*) calloc(sizeof(GltfImage), out_asset->num_images);

                for (uint32_t i = 0; i < out_asset->num_images; ++i) {
                    const JsonObject* json_image = json_array_get_object(json_images, i);
                
                    uint32_t buffer_view_index;
                    if (json_value_as_uint32(json_object_get_value(json_image, "bufferView"), &buffer_view_index)) {
                        //This shouldn't be a failure case if we have a URI-based image
                        out_asset->images[i].buffer_view = &out_asset->buffer_views[buffer_view_index];
                    }
                    //TODO: when adding support for GLTF (not just GLB), support URI-based images
                }
            }
        }

        //TODO: Samplers

        //TEXTURES
        {
            const JsonArray* json_textures = json_object_get_array(&out_asset->json, "textures");
            if (json_textures) {
                out_asset->num_textures = json_textures->count;
                out_asset->textures = (GltfTexture*) calloc(sizeof(GltfTexture), out_asset->num_textures);

                for (uint32_t i = 0; i < out_asset->num_textures; ++i) {
                    const JsonObject* json_texture = json_array_get_object(json_textures, i);

                    uint32_t image_index;
                    if (json_value_as_uint32(json_object_get_value(json_texture, "source"), &image_index)) {
                        out_asset->textures[i].image = &out_asset->images[image_index];
                    }
                }
            }
        }
        
        //MATERIALS
        {
            const JsonArray* json_materials = json_object_get_array(&out_asset->json, "materials");
            if (json_materials) {
                out_asset->num_materials = json_materials->count;
                out_asset->materials = (GltfMaterial*) calloc(sizeof(GltfMaterial), out_asset->num_materials);

                for (uint32_t i = 0; i < out_asset->num_materials; ++i) {
                    const JsonObject* json_material = json_array_get_object(json_materials, i);
                    GltfMaterial* material = &out_asset->materials[i];

                    material->double_sided = false;
                    json_value_as_bool(json_object_get_value(json_material, "doubleSided"), &material->double_sided);

                    const JsonObject* json_pbr_metallic_roughness = json_object_get_object(json_material, "pbrMetallicRoughness");
                    if (json_pbr_metallic_roughness) {
                        GltfPbrMetallicRoughness* pbr_metallic_roughness = &material->pbr_metallic_roughness;
                        
                        //TODO: base color factor

                        const JsonObject* json_base_color_texture = json_object_get_object(json_pbr_metallic_roughness, "baseColorTexture");
                        if (json_base_color_texture)
                        {
                            uint32_t base_color_texture_index;
                            if (json_value_as_uint32(json_object_get_value(json_base_color_texture, "index"), &base_color_texture_index))
                            {
                                pbr_metallic_roughness->base_color_texture = &out_asset->textures[base_color_texture_index];
                            }

                            pbr_metallic_roughness->base_color_tex_coord = 0;
                            json_value_as_uint32(json_object_get_value(json_base_color_texture, "texCoord"), &pbr_metallic_roughness->base_color_tex_coord);
                        }

                        pbr_metallic_roughness->metallic_factor = 1.0;
                        json_value_as_float(json_object_get_value(json_pbr_metallic_roughness, "metallicFactor"), &pbr_metallic_roughness->metallic_factor);
                        
                        pbr_metallic_roughness->roughness_factor = 1.0;
                        json_value_as_float(json_object_get_value(json_pbr_metallic_roughness, "roughnessFactor"), &pbr_metallic_roughness->roughness_factor);

                        const JsonObject* json_metallic_roughness_texture = json_object_get_object(json_pbr_metallic_roughness, "metallicRoughnessTexture");
                        if (json_metallic_roughness_texture)
                        {
                            uint32_t metallic_roughness_texture_index;
                            if (json_value_as_uint32(json_object_get_value(json_metallic_roughness_texture, "index"), &metallic_roughness_texture_index))
                            {
                                pbr_metallic_roughness->metallic_roughness_texture = &out_asset->textures[metallic_roughness_texture_index];
                            }

                            pbr_metallic_roughness->metallic_roughness_tex_coord = 0;
                            json_value_as_uint32(json_object_get_value(json_metallic_roughness_texture, "texCoord"), &pbr_metallic_roughness->metallic_roughness_tex_coord);
                        }
                    }
                }
            }
        }

        fclose(file);
        return true;
    }

    return false;
}

//FIXME: better checks above
//FIXME: Outline guarantees (i.e. For a successfully loaded asset, an accessors buffer view is non-null, a buffer_views bufer is non-null, etc.)

void gltf_free_asset(GltfAsset* asset) {

    for (uint32_t i = 0; i < asset->num_meshes; ++i) {
        free(asset->meshes[i].primitives);
    }
    free(asset->meshes);

    free(asset->accessors);
    free(asset->buffer_views);

    for (uint32_t i = 0; i < asset->num_buffers; ++i) {
        free(asset->buffers[i].data);
    }
    free(asset->buffers);
    free(asset->images);
    free(asset->textures);

    free_json_object(&asset->json);
}