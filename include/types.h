#ifndef TYPES_H_
#define TYPES_H_

#include <string>
typedef enum struct dtype {
    integer,
    string,
    func,
    object,
    array,
    nil,
    boolean,
    cfunction,
    struct_type,
    class_type,
    instance,
    module
} dtype_t;

inline dtype_t str_to_dtype(std::string dt) {
    if (dt == "int") {
        return dtype::integer;
    }

    if (dt == "str") {
        return dtype::string;
    }

    if (dt == "func") {
        return dtype::func;
    }

    if (dt == "object") {
        return dtype::object;
    }

    if (dt == "array") {
        return dtype::array;
    }

    if (dt == "none") {
        return dtype::nil;
    }

    if (dt == "boolean") {
        return dtype::boolean;
    }

    if (dt == "struct") {
        return dtype::struct_type;
    }

    if (dt == "class") {
        return dtype::class_type;
    }

    if (dt == "module") {
        return dtype::module;
    }

    return dtype::nil;
}

inline std::string dtype_to_str(dtype_t dt) {
    switch (dt) {
        case dtype::integer:
            return "int";

        case dtype::string:
            return "string";

        case dtype::func:
            return "function";

        case dtype::array:
            return "array";

        case dtype::object:
            return "object";

        case dtype::nil:
            return "nil";

        case dtype::cfunction:
            return "cfunc";

        case dtype::boolean:
            return "bool";

        case dtype::struct_type:
            return "struct";

        case dtype::class_type:
            return "class";

        case dtype::instance:
            return "instance";

        case dtype::module:
            return "module";
    }
    return "unknown";
}

#endif // TYPES_H_