#include "interpreter.h"
#include "ast.h"
#include "builtin.h"
#include "env.h"
#include "futil.h"
#include "parser.h"
#include "position.h"
#include "runtime.h"
#include "types.h"
#include <cstdio>
#include <error.h>
#include <string>
#include <vector>
#include <iostream>

rt_value* print(std::vector<rt_value*> args, void* env) {
    //std::string fin;
    for (int i = 0; i < args.size(); i++) {
        auto elem = args[i];
        if (i != args.size() - 1) {
            elem->out(); printf(", ");
        } else {
            elem->out(); printf("\n");
        }
    }

    //printf("%s\n", fin.c_str());
    return new rt_value();
}

std::string repeat(std::string str, const std::size_t n)
{
    if (n == 0) {
        str.clear();
        str.shrink_to_fit();
        return str;
    } else if (n == 1 || str.empty()) {
        return str;
    }
    const auto period = str.size();
    if (period == 1) {
        str.append(n - 1, str.front());
        return str;
    }
    str.reserve(period * n);
    std::size_t m {2};
    for (; m < n; m *= 2) str += str;
    str.append(str.c_str(), (n - (m / 2)) * period);
    return str;
}

#include <functional>
#include <type_traits>

rt_value_t* interpreter::run()
{
    rt_value_t* rt_val;
    environment_t* scope = new environment();
    def_on_env(scope);
    scope->interpret = this;

    // rlly hacky but
    rt_value* arg0;

    scope->assign("print", new rt_value((std::function<rt_value*(std::vector<rt_value*>, void*)>)print));

    ast_node* root = p.parse();
    //print_node(root);
    rt_val = eval_scope_samenv(root, scope);

    return rt_val;
}

rt_value_t* interpreter::eval(ast_node* node, environment_t* env)
{
    if (node == nullptr) return new rt_value();
    switch (node->type) {
        case ast_type::ast_identifier:
            return env->get_var(node->symbol);

        case ast_type::ast_return:
            return eval(node->value, env);

        case ast_type::ast_assign:
            return eval_assign(node, env);

        case ast_type::ast_compound:
            return eval_scope(node, env);

        case ast_type::ast_num_expr:
            return new rt_value(node->number);

        case ast_type::ast_string_expr:
            return new rt_value(node->symbol);

        case ast_type::ast_function:
            return eval_function(node, env);

        case ast_type::ast_call:
            return eval_call(node, env);

        case ast_type::ast_array:
            return eval_array(node, env);

        case ast_type::ast_object:
            return eval_object(node, env);

        case ast_type::ast_member:
            return eval_member(node, env);

        case ast_type::ast_noop:
            return new rt_value();

        case ast_type::ast_arrindex:
            return eval_arrindex(node, env);

        case ast_type::ast_import:
            return eval_import(node, env);

        case ast_type::ast_binop:
            return eval_binary(node, env);

        case ast_type::ast_unaryop:
            return eval_unary(node, env);

        case ast_type::ast_if:
            return eval_if(node, env);

        case ast_type::ast_while:
            return eval_while(node, env);

        case ast_type::ast_bool:
            return new rt_value(static_cast<bool>(node->number));

        case ast_type::ast_struct_def:
            return eval_struct_def(node, env);

        case ast_type::ast_class_def:
            return eval_class_def(node, env);
    }
    return new rt_value();
}

rt_value_t* interpreter::eval_assign(ast_node* node, environment_t* env)
{
    rt_value* value = eval(node->value, env);

    // Skip type checking if:
    // 1. No type annotation (data_type is nil)
    // 2. Value is an instance (struct/class instance)
    // 3. Value type matches expected type
    if (node->data_type != dtype::nil &&
        value->type != dtype::instance &&
        value->type != node->data_type) {
        parse_error(string_format("expected type %s for %s, got %s",
            dtype_to_str(node->data_type).c_str(),
            node->symbol.c_str(),
            dtype_to_str(value->type).c_str()),
            node->pos, source).spit();
    }

    env->assign(node->symbol, value);
    return new rt_value();
}

rt_value_t* interpreter::eval_object(ast_node* node, environment_t* env) {
    std::map<std::string, rt_value*> object;

    for (auto elem : node->children) {
        object[elem->symbol] = eval(elem->value, env);
    }

    return new rt_value(object);
}

rt_value_t* interpreter::eval_array(ast_node* node, environment_t* env)
{
    std::vector<rt_value*> arr;

    for (auto elem : node->children) {
        arr.push_back(eval(elem, env));
    }

    return new rt_value(arr);
}

rt_value_t* interpreter::eval_function(ast_node* node, environment_t* env)
{
    rt_value_t* fc = new rt_value(node->value, node);
    return fc;
}

rt_value_t* interpreter::eval_scope(ast_node* node, environment_t* env)
{
    rt_value_t* rt_val;
    environment_t* s = new environment(env);

    for (ast_node* elem : node->children) {
        if (elem->type == ast_type::ast_return) rt_val = eval(elem, s);
        else eval(elem, s);
    }

    return rt_val;
}

rt_value_t* interpreter::eval_scope_samenv(ast_node* node, environment_t* env)
{
    rt_value_t* rt_val;

    for (ast_node* elem : node->children) {
        if (elem->type == ast_type::ast_return) rt_val = eval(elem, env);
        else eval(elem, env);
    }

    return rt_val;
}






rt_value_t* interpreter::eval_call(ast_node* node, environment_t* env)
{
    rt_value_t* scope = env->get_var(node->symbol);

    // Handle struct instantiation
    if (scope && scope->type == dtype::struct_type) {
        return eval_struct_instantiate(scope, node, env);
    }

    // Handle class instantiation
    if (scope && scope->type == dtype::class_type) {
        return eval_class_instantiate(scope, node, env);
    }

    if (scope && (scope->type == dtype::func || scope->type == dtype::cfunction)) {
        environment_t* cenv = new environment(env);
        std::vector<rt_value*> args;

        for (int i = 0; i < scope->proto->children.size(); i++) {
            ast_node* arg = node->value->children[i];
            if (arg == nullptr) break;
            ast_node* id = scope->proto->children[i];
            if (id == nullptr) break;
            rt_value* evaluated = eval(arg, env);
            if (evaluated->type != id->data_type && scope->type != dtype::cfunction) {
                parse_error(string_format("expected type %s for argument %s, got %s", dtype_to_str(id->data_type).c_str(), id->symbol.c_str(), dtype_to_str(evaluated->type).c_str()), node->pos, source).spit();
                return nullptr;
            }
            args.push_back(evaluated);
            cenv->assign(id->symbol, evaluated);
        }

        rt_value_t* rt_val;
        dtype_t ftype = scope->proto->data_type;

        if (ftype != dtype::cfunction) {
            std::vector<ast_node*> body = scope->body->children;
            if (body.size() > 0) {
                for (ast_node* elem : body) {
                    if (elem->type == ast_type::ast_return) rt_val = eval(elem, cenv);
                    else eval(elem, cenv);
                }
            }
        } else {
            rt_val = scope->cfunc(args, env);
        }

        return rt_val;
    } else {
        // Handle the case where the function is not found
        parse_error("function not found: " + node->symbol, node->pos, source).spit();
        return nullptr;
    }
}




rt_value_t* interpreter::call_func(rt_value* func, std::vector<rt_value*> args, environment_t* env)
{
    environment_t* cenv = new environment(env);

    // Check if func and func->proto have valid elements
    if (!func || !func->proto) {
        parse_error("invalid function or function prototype", func ? func->body->pos : position(), source).spit();
        return nullptr;
    }

    // Check if args size matches the number of parameters in func->proto
    if (args.size() != func->proto->children.size()) {
        parse_error("mismatched number of arguments and function parameters", func->proto->pos, source).spit();
        delete cenv;  // Clean up allocated environment
        return nullptr;
    }

    // Assign arguments to parameters in the new environment
    for (int i = 0; i < args.size(); i++) {
        ast_node* id = func->proto->children[i];
        if (!id) {
            parse_error("invalid function parameter", func->proto->pos, source).spit();
            delete cenv;  // Clean up allocated environment
            return nullptr;
        }
        cenv->assign(id->symbol, args[i]);
    }

    rt_value_t* rt_val = nullptr;  // Initialize to nullptr

    // Evaluate the function body
    std::vector<ast_node*> body = func->body->children;
    if (body.size() > 0) {
        for (ast_node* elem : body) {
            if (elem->type == ast_type::ast_return) rt_val = eval(elem, cenv);
            else eval(elem, cenv);
        }
    }

    delete cenv;  // Clean up allocated environment

    return rt_val;
}


rt_value_t* interpreter::eval_call(ast_node* node, environment_t* env, rt_value* func)
{
    rt_value_t* scope = func;
    environment_t* cenv = new environment(env);
    std::vector<rt_value*> args;

    for (int i = 0; i < scope->proto->children.size(); i++) {
        if (node->value->children.size() <= i && scope->proto->data_type != dtype::cfunction) parse_error(string_format("expected %d args, got %d", scope->proto->children.size(), node->value->children.size()), node->pos, source).spit();
        ast_node* arg = node->value->children[i];
        if (arg == nullptr) break;
        ast_node* id = scope->proto->children[i];
        if (id == nullptr) break;
        rt_value* evaluated = eval(arg, env);
        if (evaluated->type != id->data_type && scope->proto->data_type != dtype::cfunction) parse_error(string_format("expected type %s for argument %s, got %s", dtype_to_str(id->data_type).c_str(), id->symbol.c_str(), dtype_to_str(evaluated->type).c_str()), node->pos, source).spit();
        args.push_back(evaluated);
        cenv->assign(id->symbol, evaluated);
    }

    rt_value_t* rt_val;
    dtype_t ftype = scope->proto->data_type;

    if (ftype != dtype::cfunction) {
        std::vector<ast_node*> body = scope->body->children;
        if (body.size() > 0) {
            for (ast_node* elem : body) {
                if (elem->type == ast_type::ast_return) rt_val = eval(elem, cenv);
                else eval(elem, cenv);
            }
        }
    } else {
        rt_val = scope->cfunc(args, env);
    }

    return rt_val;
}










    

rt_value_t* interpreter::eval_member(ast_node* node, environment_t* env) {
    // Get the left-hand side symbol directly
    std::string member_name = node->symbol;

    // Retrieve the object from the environment
    rt_value_t* obj = env->get_var(member_name);

    if (obj->type == dtype::object) {
        if (node->value->type == ast_type::ast_identifier) {
            // Single member access, return the corresponding value
            std::string member_symbol = node->value->symbol;
            if (obj->children.find(member_symbol) != obj->children.end()) {
                return obj->children[member_symbol];
            } else {
                // Handle member not found error
                parse_error(string_format("member %s not found", member_symbol.c_str()), node->value->pos, source).spit();
            }
        } else {
            // Traverse member expressions
            ast_node* current_node = node->value;

            while (current_node->type == ast_type::ast_member) {
                std::string current_member_symbol = current_node->symbol;
                if (obj->children.find(current_member_symbol) != obj->children.end()) {
                    obj = obj->children[current_member_symbol];
                    current_node = current_node->value;
                    
                    if (current_node->type == ast_type::ast_call) {
                        obj = obj->children[current_node->symbol];
                        return eval_call(current_node, env, obj);
                    }
                } else {
                    // Handle member not found error
                    parse_error(string_format("member %s not found", current_member_symbol.c_str()), current_node->pos, source).spit();
                }
            }

            // Evaluate the final member access
            if (current_node->type == ast_type::ast_identifier) {
                std::string final_member_symbol = current_node->symbol;
                if (obj->children.find(final_member_symbol) != obj->children.end()) {
                    return obj->children[final_member_symbol];
                } else {
                    // Handle member not found error
                    parse_error(string_format("member %s not found", final_member_symbol.c_str()), current_node->pos, source).spit();
                }
            }

            if (current_node->type == ast_type::ast_call) {
                obj = obj->children[current_node->symbol];
                return eval_call(current_node, env, obj);
            }
        }
    } else {
        // Handle error when trying to access a member on a non-object
        parse_error(string_format("not an object"), node->pos, source).spit();
    }
    
    return new rt_value();
}

rt_value_t* interpreter::eval_arrindex(ast_node* node, environment_t* env)
{
    rt_value_t* arr = env->get_var(node->symbol);
    if (arr->type != dtype::array) {
        if (arr->type != dtype::object) parse_error(string_format("not an array or object"), node->pos, source).spit();
        rt_value_t* idx = eval(node->value, env);
        if (idx->type != dtype::string) parse_error(string_format("not an indexable type for object"), node->pos, source).spit();
        return arr->children[idx->str];
    }

    if (node->value->type == ast_type::ast_num_expr) {
        return arr->arr[node->value->number];
    } else {
        rt_value_t* idx = eval(node->value, env);
        if (idx->type != dtype::integer) parse_error(string_format("not an indexable type for array"), node->pos, source).spit();
        return arr->arr[idx->num];
    }
}

rt_value_t* interpreter::eval_import(ast_node* node, environment_t* env)
{
    ast_node* path = node->value;
    ast_node* id = node->svalue;

    rt_value* strpath = eval(path, env);

    if (strpath->type == dtype::string) {
        std::string contents = futil::read_file(strpath->str.c_str());
        interpreter_t i(contents);
        rt_value* res = i.run();
        env->assign(id->symbol, res);
    } else {
        parse_error("invalid arguments to import", path->pos, source).spit();
    }

    return new rt_value();
}

rt_value_t* interpreter::eval_unary(ast_node* node, environment_t* env)
{
    rt_value* operand = eval(node->value, env);
    std::string op = node->symbol;

    if (op == "!") {
        if (operand->type == dtype::boolean) {
            return new rt_value(!operand->boolean);
        } else {
            // Truthy/falsy evaluation
            bool is_truthy = (operand->type != dtype::nil);
            return new rt_value(!is_truthy);
        }
    }

    return new rt_value();
}

rt_value_t* interpreter::eval_binary(ast_node* node, environment_t* env)
{
    rt_value* left = eval(node->value, env);
    rt_value* right = eval(node->svalue, env);
    ///print_node(node->svalue);

    if (left->type == dtype::integer && right->type == dtype::integer) {
        std::string op = node->symbol;

        if (op == "+") return new rt_value(left->num + right->num);
        if (op == "-") return new rt_value(left->num - right->num);
        if (op == "/") return new rt_value(left->num / right->num);
        if (op == "*") return new rt_value(left->num * right->num);
        if (op == "==") return new rt_value(left->num == right->num);
        if (op == "!=") return new rt_value(left->num != right->num);
        if (op == ">=") return new rt_value(left->num >= right->num);
        if (op == "<=") return new rt_value(left->num <= right->num);
        if (op == "<") return new rt_value(left->num < right->num);
        if (op == ">") return new rt_value(left->num > right->num);
    }

    if (left->type == dtype::string && right->type == dtype::string) {
        std::string op = node->symbol;

        if (op == "+") return new rt_value(left->str + right->str);
        if (op == "-") parse_error("cannot sub string by string", node->pos, source).spit();
        if (op == "/") parse_error("cannot divide string by string", node->pos, source).spit();
        if (op == "*") parse_error("cannot multiply string by string", node->pos, source).spit();
        if (op == "==") return new rt_value(left->str == right->str);
        if (op == "!=") return new rt_value(left->str != right->str);
        if (op == ">=") parse_error("cannot check if string is greater than or equal to string", node->pos, source).spit();
        if (op == "<=") parse_error("cannot check if string is less than or equal to string", node->pos, source).spit();
        if (op == "<") parse_error("cannot check if string is less than string", node->pos, source).spit();
        if (op == ">") parse_error("cannot check if string is greater than string", node->pos, source).spit();
    }

    if (left->type == dtype::string && right->type == dtype::integer) {
        std::string op = node->symbol;

        if (op == "+") return new rt_value(left->str + std::to_string(right->num));
        if (op == "-") parse_error("cannot sub string by number", node->pos, source).spit();
        if (op == "/") parse_error("cannot divide string by number", node->pos, source).spit();
        if (op == "*") return new rt_value(repeat(left->str, (int)right->num));
        if (op == ">=") parse_error("cannot check if string is greater than or equal to number", node->pos, source).spit();
        if (op == "<=") parse_error("cannot check if string is less than or equal to number", node->pos, source).spit();
        if (op == "<") parse_error("cannot check if string is less than number", node->pos, source).spit();
        if (op == ">") parse_error("cannot check if string is greater than number", node->pos, source).spit();
    }

    if (left->type == dtype::boolean && right->type == dtype::boolean) {
        std::string op = node->symbol;

        if (op == "&&") return new rt_value(left->boolean && right->boolean);
        if (op == "||") return new rt_value(left->boolean || right->boolean);
        if (op == "==") return new rt_value(left->boolean == right->boolean);
        if (op == "!=") return new rt_value(left->boolean != right->boolean);
    }

    // Handle unary not operator
    if (node->symbol == "!" && right->type == dtype::boolean) {
        return new rt_value(!right->boolean);
    }

    return new rt_value();
}

rt_value_t* interpreter::eval_if(ast_node* node, environment_t* env)
{
    rt_value* evaluated = eval(node->svalue, env);
    bool condition_true = false;

    if (evaluated->type == dtype::boolean) {
        condition_true = evaluated->boolean;
    } else {
        condition_true = (evaluated->type != dtype::nil);
    }

    if (condition_true) {
        eval_scope_samenv(node->value, env);
    } else if (!node->children.empty()) {
        // Execute else clause if it exists
        eval_scope_samenv(node->children[0], env);
    }

    return new rt_value();
}

rt_value_t* interpreter::eval_while(ast_node* node, environment_t* env)
{
    rt_value* evaluated = eval(node->svalue, env);
    if (evaluated->type == dtype::boolean) {
        while (evaluated->boolean == true) {
            eval_scope_samenv(node->value, env);
            evaluated = eval(node->svalue, env);
        }
    } else {
        while (evaluated->type != dtype::nil) {
            eval_scope_samenv(node->value, env);
            evaluated = eval(node->svalue, env);
        }
    }

    return new rt_value();
}
rt_value_t* interpreter::eval_struct_def(ast_node* node, environment_t* env)
{
    // Create a struct type that can be used as a constructor
    rt_value* struct_type = new rt_value();
    struct_type->type = dtype::struct_type;
    struct_type->type_name = node->symbol;
    struct_type->type_def = node;  // Store the definition

    // Store the struct type in the environment
    env->assign(node->symbol, struct_type);

    return new rt_value(); // Struct definition doesn't return a value
}

rt_value_t* interpreter::eval_class_def(ast_node* node, environment_t* env)
{
    // Create a class type that can be used as a constructor
    rt_value* class_type = new rt_value();
    class_type->type = dtype::class_type;
    class_type->type_name = node->symbol;
    class_type->type_def = node;  // Store the definition

    // Store the class type in the environment
    env->assign(node->symbol, class_type);

    return new rt_value(); // Class definition doesn't return a value
}

rt_value_t* interpreter::eval_struct_instantiate(rt_value* struct_type, ast_node* node, environment_t* env)
{
    // Get the struct definition
    ast_node* def = struct_type->type_def;

    // Create instance fields from the arguments
    std::map<std::string, rt_value*> fields;

    // Match arguments to fields by position
    for (size_t i = 0; i < def->children.size() && i < node->value->children.size(); i++) {
        ast_node* field_def = def->children[i];
        ast_node* arg = node->value->children[i];

        rt_value* field_value = eval(arg, env);

        // Type checking
        if (field_value->type != field_def->data_type && field_def->data_type != dtype::nil) {
            parse_error(string_format("expected type %s for field %s, got %s",
                dtype_to_str(field_def->data_type).c_str(),
                field_def->symbol.c_str(),
                dtype_to_str(field_value->type).c_str()),
                node->pos, source).spit();
        }

        fields[field_def->symbol] = field_value;
    }

    // Create the struct instance
    rt_value* instance = new rt_value(struct_type->type_name, fields, def);

    return instance;
}

rt_value_t* interpreter::eval_class_instantiate(rt_value* class_type, ast_node* node, environment_t* env)
{
    // Get the class definition
    ast_node* def = class_type->type_def;

    // Separate fields and methods from the class definition
    std::map<std::string, rt_value*> fields;
    std::map<std::string, rt_value*> methods;

    // Parse the class definition to get fields and methods
    for (auto member : def->children) {
        if (member->type == ast_type::ast_function) {
            // It's a method
            rt_value* method = new rt_value(member->value, member);
            methods[member->symbol] = method;
        } else if (member->type == ast_type::ast_member) {
            // It's a field - initialize to nil, will be set by constructor args
            fields[member->symbol] = new rt_value();  // nil default
        }
    }

    // Set field values from constructor arguments
    size_t arg_index = 0;
    for (auto member : def->children) {
        if (member->type == ast_type::ast_member && arg_index < node->value->children.size()) {
            ast_node* arg = node->value->children[arg_index];
            rt_value* field_value = eval(arg, env);

            // Type checking
            if (field_value->type != member->data_type && member->data_type != dtype::nil) {
                parse_error(string_format("expected type %s for field %s, got %s",
                    dtype_to_str(member->data_type).c_str(),
                    member->symbol.c_str(),
                    dtype_to_str(field_value->type).c_str()),
                    node->pos, source).spit();
            }

            fields[member->symbol] = field_value;
            arg_index++;
        }
    }

    // Create the class instance
    rt_value* instance = new rt_value(class_type->type_name, fields, methods, def);

    return instance;
}
