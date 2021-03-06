#include <detail/semantic.h>
#include <lexer.h>
#include <parser.h>
#include <config.h>

#include <memory>
#include <utility>
#include <iostream>
#include <fstream>
#include <cassert>

namespace avm {
SemanticAnalyzer::SemanticAnalyzer(CompilerState *state_ptr)
    : state_ptr(state_ptr)
{
}

void SemanticAnalyzer::Analyze(AstModule *ast)
{
    Accept(ast);

    LevelInfo &level = state_ptr->CurrentLevel();
    for (auto &&local : level.locals) {
        if (local.second.node != nullptr) {
            size_t use_count = state_ptr->use_counts[local.second.node];
            if (use_count == 0) {
                WarningMessage(Msg_unused_identifier, 
                    local.second.node->location, local.second.original_name);
            }
        }
    }

    state_ptr->block_id_counter = 0;
    state_ptr->level = compiler_global_level;
    state_ptr->levels.clear();
}

void SemanticAnalyzer::AddModule(const ModuleDefine &def)
{
    // first, check that it wasn't already defined
    auto unit = std::unique_ptr<AstModule>(new AstModule(SourceLocation(-1, -1, ""), def.name));
    if (state_ptr->FindModule(def.name)) {
        ErrorMsg(Msg_module_already_defined, unit->location, unit->name);
    } else {
        for (auto &&meth : def.methods) {
            std::string var_name = state_ptr->MakeVariableName(meth.name, unit.get());

            for (auto &&var : state_ptr->CurrentLevel().locals) {
                if (var.first == meth.name) {
                    ErrorMsg(Msg_redeclared_identifier, SourceLocation(-1, -1, ""));
                    return;
                }
            }

            auto &level = state_ptr->CurrentLevel();
            Symbol symbol;
            symbol.original_name = meth.name;
            symbol.nargs = meth.nargs;
            symbol.is_native = true;
            symbol.owner_level = state_ptr->level;
            symbol.field_index = state_ptr->CurrentLevel().locals.size();
            level.locals.push_back({ var_name, symbol });
        }

        state_ptr->other_modules[def.name] = std::move(unit);
    }
}

void SemanticAnalyzer::Accept(AstModule *node)
{
    for (auto &&child : node->children) {
        Accept(child.get());
    }
}

void SemanticAnalyzer::Accept(AstNode *node)
{
    if (!node) {
        return;
    }

    switch (node->type) {
    case Ast_type_imports:
        Accept(static_cast<AstImports*>(node));
        break;
    case Ast_type_import:
        Accept(static_cast<AstImport*>(node));
        break;
    case Ast_type_statement:
        Accept(static_cast<AstStatement*>(node));
        break;
    case Ast_type_block:
        Accept(static_cast<AstBlock*>(node));
        break;
    case Ast_type_expression:
        Accept(static_cast<AstExpression*>(node));
        break;
    case Ast_type_binop:
        Accept(static_cast<AstBinaryOp*>(node));
        break;
    case Ast_type_unop:
        Accept(static_cast<AstUnaryOp*>(node));
        break;
    case Ast_type_array_access:
        Accept(static_cast<AstArrayAccess*>(node));
        break;
    case Ast_type_member_access:
        Accept(static_cast<AstMemberAccess*>(node));
        break;
    case Ast_type_module_access:
        Accept(static_cast<AstModuleAccess*>(node));
        break;
    case Ast_type_var_declaration:
        Accept(static_cast<AstVariableDeclaration*>(node));
        break;
    case Ast_type_alias:
        Accept(static_cast<AstAlias*>(node));
        break;
    case Ast_type_use_module:
        Accept(static_cast<AstUseModule*>(node));
        break;
    case Ast_type_variable:
        Accept(static_cast<AstVariable*>(node));
        break;
    case Ast_type_integer:
        Accept(static_cast<AstInteger*>(node));
        break;
    case Ast_type_float:
        Accept(static_cast<AstFloat*>(node));
        break;
    case Ast_type_string:
        Accept(static_cast<AstString*>(node));
        break;
    case Ast_type_true:
        Accept(static_cast<AstTrue*>(node));
        break;
    case Ast_type_false:
        Accept(static_cast<AstFalse*>(node));
        break;
    case Ast_type_null:
        Accept(static_cast<AstNull*>(node));
        break;
    case Ast_type_self:
        Accept(static_cast<AstSelf*>(node));
        break;
    case Ast_type_new:
        Accept(static_cast<AstNew*>(node));
        break;
    case Ast_type_function_definition:
        Accept(static_cast<AstFunctionDefinition*>(node));
        break;
    case Ast_type_function_expression:
        Accept(static_cast<AstFunctionExpression*>(node));
        break;
    case Ast_type_function_call:
        Accept(static_cast<AstFunctionCall*>(node));
        break;
    case Ast_type_class_declaration:
        Accept(static_cast<AstClass*>(node));
        break;
    case Ast_type_object_expression:
        Accept(static_cast<AstObjectExpression*>(node));
        break;
    case Ast_type_enum:
        Accept(static_cast<AstEnum*>(node));
        break;
    case Ast_type_print:
        Accept(static_cast<AstPrintStmt*>(node));
        break;
    case Ast_type_return:
        Accept(static_cast<AstReturnStmt*>(node));
        break;
    case Ast_type_if_statement:
        Accept(static_cast<AstIfStmt*>(node));
        break;
    case Ast_type_for_loop:
        Accept(static_cast<AstForLoop*>(node));
        break;
    case Ast_type_while_loop:
        Accept(static_cast<AstWhileLoop*>(node));
        break;
    case Ast_type_try_catch:
        Accept(static_cast<AstTryCatch*>(node));
        break;
    default:
        // not implemented?
        ErrorMsg(Msg_internal_error, node->location);
        break;
    }
}

void SemanticAnalyzer::Accept(AstImports *node)
{
    for (auto &child : node->children) {
        Accept(child.get());
    }
}

void SemanticAnalyzer::Accept(AstImport *node)
{
    if (state_ptr->level != compiler_global_level) {
        ErrorMsg(Msg_import_outside_global, node->location);
    }

    // load relative file
    std::string path = node->relative_path + node->import_str;

    // Check if the module has already been imported
    if (state_ptr->other_modules.find(path) == state_ptr->other_modules.end()) {
        std::ifstream file(path);

        if (!file.is_open()) {
            ErrorMsg(Msg_import_not_found, node->location, node->import_str, path);
        } else {
            std::string str((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            file.close();

            Lexer lexer(str, path);
            auto tokens = lexer.ScanTokens();

            Parser parser(tokens, lexer.state);
            auto unit = parser.Parse();

            bool already_imported = false;
            for (auto &&it : state_ptr->other_modules) {
                if (it.second->name == unit->name) {
                    already_imported = true;
                    break;
                }
            }

            if (!already_imported) {
                state_ptr->other_modules[path] = std::move(unit);

                for (auto &&error : parser.state.errors) {
                    state_ptr->errors.push_back(error);
                }

                auto &mod = state_ptr->other_modules[path];
                if (mod != nullptr) {
                    for (auto &&child : mod->children) {
                        Accept(child.get());
                    }
                }
            } else {
                ErrorMsg(Msg_module_already_defined, node->location, unit->name);
            }
        }
    }
}

void SemanticAnalyzer::Accept(AstStatement *node)
{
}

void SemanticAnalyzer::Accept(AstBlock *node)
{
    bool in_dead_code = false;
    bool warning_shown = false;

    size_t counter = 0;
    while (counter < node->children.size()) {
        auto &child = node->children[counter];
        Accept(child.get());

        if (child) {
            if (child->type == Ast_type_return) {
                in_dead_code = true;
            } else if (in_dead_code && !warning_shown && child->type != Ast_type_statement) {
                WarningMessage(Msg_unreachable_code, child->location);
                warning_shown = true;
            }
        }

        ++counter;
    }
}

void SemanticAnalyzer::Accept(AstExpression *node)
{
    Accept(node->child.get());
}

void SemanticAnalyzer::Accept(AstBinaryOp *node)
{
    auto &left = node->left;
    auto &right = node->right;

    Accept(left.get());
    Accept(right.get());

    switch (node->op) {
    case BinOp_assign:
        // assignment, change the set type
        if (left->type == Ast_type_variable) {
            auto *casted = static_cast<AstVariable*>(left.get());
            if (!casted->is_const) {
                auto *right_side = right.get();
                if (right->type == Ast_type_expression) {
                    // get inner child
                    auto *expr_ast = static_cast<AstExpression*>(right.get());
                    right_side = expr_ast->child.get();
                }

                std::unique_ptr<AstNode> right_side_opt = nullptr;
                if (config::optimize_constant_folding) {
                    right_side_opt = right_side->Optimize();
                    if (right_side_opt != nullptr) {
                        right_side = right_side_opt.get();
                    }
                }

                casted->symbol_ptr->current_value = right.get();
                casted->current_value = casted->symbol_ptr->current_value;
                switch (right_side->type) {
                    // set is_literal only if it is a literal type (int, float or string)
                case Ast_type_integer:
                case Ast_type_float:
                case Ast_type_string:
                    casted->symbol_ptr->is_literal = true;
                    break;
                default:
                    casted->symbol_ptr->is_literal = false;
                    break;
                }
            }
        }
    // FALLTHROUGH
    case BinOp_add_assign:
    case BinOp_subtract_assign:
    case BinOp_multiply_assign:
    case BinOp_divide_assign:
        if (left->type == Ast_type_variable) {
            auto *casted = static_cast<AstVariable*>(left.get());
            if (casted->is_const) {
                ErrorMsg(Msg_const_identifier, casted->location, casted->name);
            }

            // This usage is prohibited on inlined objects
            if (left->HasAttribute("inline")) {
                ErrorMsg(Msg_prohibited_action_attribute, left->location, "inline");
            }
            if (right->HasAttribute("inline")) {
                ErrorMsg(Msg_prohibited_action_attribute, right->location, "inline");
            }
        } else if (left->type == Ast_type_member_access) {
            /// \todo: check const
        } else if (left->type == Ast_type_array_access) {
            /// \todo: check const
        } else {
            ErrorMsg(Msg_expected_identifier, left->location);
        }
        break;
    }
}

void SemanticAnalyzer::Accept(AstUnaryOp *node)
{
    Accept(node->child.get());
}

void SemanticAnalyzer::Accept(AstArrayAccess *node)
{
    Accept(node->object.get());
    Accept(node->index.get());
}

void SemanticAnalyzer::Accept(AstMemberAccess *node)
{
    // checks for module with name first
    AstModule *found_module = nullptr;
    if (state_ptr->FindModule(node->left_str, node->module, found_module)) {
        // it is a module being referenced
        // set the right node's module to be the one we found
        node->right->module = found_module;
        Accept(node->right.get());
    } else {
        Accept(node->left.get());
        if (node->right->type == Ast_type_member_access) {
            Accept(node->right.get());
        } else if (node->right->type != Ast_type_variable && node->right->type != Ast_type_function_call) {
            ErrorMsg(Msg_internal_error, node->location);
        }
    }
}

void SemanticAnalyzer::Accept(AstModuleAccess *node)
{
    for (auto &&mod : state_ptr->other_modules) {
        if (mod.second->name == node->module_name) {
            node->right->module = mod.second.get();
            Accept(node->right.get());
            return;
        }
    }
    ErrorMsg(Msg_module_not_imported, node->location, node->module_name);
}

void SemanticAnalyzer::Accept(AstVariableDeclaration *node)
{
    //node->is_const = node->HasAttribute("const");

    std::string var_name = state_ptr->MakeVariableName(node->name, node->module);
    if (FindVariable(var_name, true)) {
        ErrorMsg(Msg_redeclared_identifier, node->location, node->name);
    } else if (state_ptr->FindModule(node->name, node->module)) {
        ErrorMsg(Msg_identifier_is_module, node->location, node->name);
    } else {
        auto &level = state_ptr->CurrentLevel();

        Symbol symbol;
        symbol.node = node;
        symbol.original_name = node->name;
        symbol.is_const = node->is_const;
        symbol.current_value = node->assignment.get();

        AstNode *right_side = symbol.current_value;
        if (right_side->type == Ast_type_expression) {
            // get inner child
            AstExpression *expr_ast = static_cast<AstExpression*>(symbol.current_value);
            right_side = expr_ast->child.get();
        }

        std::unique_ptr<AstNode> right_side_opt = nullptr;
        if (config::optimize_constant_folding) {
            right_side_opt = right_side->Optimize();
            if (right_side_opt != nullptr) {
                right_side = right_side_opt.get();
            }
        }

        switch (right_side->type) {
        case Ast_type_integer:
        case Ast_type_float:
        case Ast_type_string:
            symbol.is_literal = true;
            break;
        default:
            symbol.is_literal = false;
            break;
        }

        symbol.owner_level = state_ptr->level;
        symbol.field_index = state_ptr->CurrentLevel().locals.size();
        level.locals.push_back({ var_name, symbol });

        Accept(node->assignment.get());
    }
}

void SemanticAnalyzer::Accept(AstAlias *node)
{
    std::string var_name = state_ptr->MakeVariableName(node->name, node->module);
    if (FindVariable(var_name, true)) {
        ErrorMsg(Msg_redeclared_identifier, node->location, node->name);
    } else if (state_ptr->FindModule(node->name, node->module)) {
        ErrorMsg(Msg_identifier_is_module, node->location, node->name);
    } else {
        Accept(node->alias_to.get());

        Symbol symbol;
        symbol.node = node->alias_to.get();
        symbol.original_name = node->name;
        symbol.is_alias = true;
        symbol.owner_level = -1;
        symbol.field_index = -1;

        AstNode *candidate = node->alias_to.get();
        while (candidate != nullptr) {
            if (candidate->type == Ast_type_member_access) {
                AstMemberAccess *ast_mem = dynamic_cast<AstMemberAccess*>(candidate);
                if (!ast_mem) { throw std::runtime_error("internal ast node error"); }
                candidate = ast_mem->right.get();
                continue;
            } else if (candidate->type == Ast_type_variable) {
                AstVariable *ast_var = dynamic_cast<AstVariable*>(candidate);
                if (!ast_var) { throw std::runtime_error("internal ast node error"); }
                // we can use the variable
                // set the field index so the symbol refers to this variable:
                symbol.owner_level = ast_var->owner_level;
                symbol.field_index = ast_var->field_index;
                break;
            } else {
                ErrorMsg(Msg_unrecognized_alias_type, node->location, node->name);
                break;
            }
        }

        state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
    }
}

void SemanticAnalyzer::Accept(AstUseModule *node)
{
    /*for (auto &&it : state_ptr->levels[0].locals) {
      AstModule *module = nullptr;
      if (it.second.node && it.second.node->module &&
          it.second.node->module->type == Ast_type_module) {
        module = static_cast<AstModule*>(it.second.node->module);
        if (module->name == node->name) {
          // add an alias for this object
          std::string var_name = state_ptr->MakeVariableName(it.second.original_name, node->module);
          if (FindVariable(var_name, true)) {
            ErrorMsg(msg_redeclared_identifier, node->location, it.second.original_name);
          } else if (state_ptr->FindModule(node->name, node->module)) {
            ErrorMsg(msg_identifier_is_module, node->location, it.second.original_name);
          } else {
            Symbol info;
            info.node = it.second.node;
            info.original_name = it.second.original_name;
            info.is_alias = true;
            state_ptr->current_level().locals.push_back({ var_name, info });
          }
        }
      }
    }*/
    ErrorMsg(Msg_unsupported_feature, node->location);
}

void SemanticAnalyzer::Accept(AstVariable *node)
{
    std::string var_name = state_ptr->MakeVariableName(node->name, node->module);
    SymbolQueryResult query = FindVariable(var_name, false);
    if (query) {
        // Copy symbol information
        node->is_alias = query.symbol->is_alias;
        if (query.symbol->is_alias) {
            node->alias_to = query.symbol->node;
        }
        node->is_const = query.symbol->is_const;
        node->is_literal = query.symbol->is_literal;
        node->current_value = query.symbol->current_value;
        node->symbol_ptr = query.symbol;
        node->owner_level = query.symbol->owner_level;
        node->field_index = query.symbol->field_index;

        if (query.symbol->node != nullptr) {
            if (query.symbol->node->type == Ast_type_function_definition) {
                if (query.symbol->node->HasAttribute("inline")) {
                    ErrorMsg(Msg_prohibited_action_attribute, node->location, "inline");
                }
            }

            // do not increment use count for const literals, they will be inlined
            if (!(config::optimize_constant_folding &&
                node->is_const && 
                node->is_literal && 
                node->current_value != nullptr)) {
                IncrementUseCount(query.symbol->node);
            }
        }
    } else {
        ErrorMsg(Msg_undeclared_identifier, node->location, node->name);
    }
}

void SemanticAnalyzer::Accept(AstInteger *node)
{
}

void SemanticAnalyzer::Accept(AstFloat *node)
{
}

void SemanticAnalyzer::Accept(AstString *node)
{
}

void SemanticAnalyzer::Accept(AstTrue *node)
{
}

void SemanticAnalyzer::Accept(AstFalse *node)
{
}

void SemanticAnalyzer::Accept(AstNull *node)
{
}

void SemanticAnalyzer::Accept(AstSelf *node)
{
}

void SemanticAnalyzer::Accept(AstNew *node)
{
}

void SemanticAnalyzer::Accept(AstFunctionDefinition *node)
{
    std::string var_name = state_ptr->MakeVariableName(node->name, node->module);
    if (FindVariable(var_name, true)) {
        ErrorMsg(Msg_redeclared_identifier, node->location, node->name);
    } else if (state_ptr->FindModule(node->name, node->module)) {
        ErrorMsg(Msg_identifier_is_module, node->location, node->name);
    } else {
        if (!node->HasAttribute("inline")) {
            Symbol symbol;
            symbol.node = node;
            symbol.original_name = node->name;
            symbol.owner_level = state_ptr->level;
            symbol.field_index = state_ptr->CurrentLevel().locals.size();
            state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
        }

        AstBlock *body = dynamic_cast<AstBlock*>(node->block.get());
        if (body) {
            if (body->children.empty()) {
                SourceLocation location = body->location;

                InfoMsg(Msg_empty_function_body, location, node->name);
                //InfoMsg(Msg_missing_final_return, node->location, node->name);

                // add return statement
                auto ret_value = std::unique_ptr<AstNull>(new AstNull(location, node->module));
                auto ret_ast = std::unique_ptr<AstReturnStmt>(new AstReturnStmt(location, node->module, std::move(ret_value)));

                body->AddChild(std::move(ret_ast));
            } else {
                bool has_return = false;

                if (!body->children.empty()) {
                    size_t idx = body->children.size() - 1;
                    if (body->children[idx] && body->children[idx]->type == Ast_type_return) {
                        has_return = true;
                    } else {
                        while (idx > 0 && body->children[idx]->type == Ast_type_statement) {
                            if (body->children[idx - 1]->type == Ast_type_return) {
                                has_return = true;
                                break;
                            } else if (body->children[idx - 1]->type != Ast_type_statement) {
                                has_return = false;
                                break;
                            }

                            --idx;
                        }
                    }
                }

                if (!has_return) {
                    SourceLocation location = body->children.back() ? body->children.back()->location : body->location;
                    // show warning
                    //InfoMsg(Msg_missing_final_return, node->location, node->name);

                    // add return statement
                    auto ret_value = std::unique_ptr<AstNull>(new AstNull(location, node->module));
                    auto ret_ast = std::unique_ptr<AstReturnStmt>(new AstReturnStmt(location, node->module, std::move(ret_value)));

                    body->AddChild(std::move(ret_ast));
                }
            }

            IncreaseBlock(LevelType::Level_function);

            // declare a variable for all parameters
            for (const std::string &param : node->arguments) {
                std::string var_name = state_ptr->MakeVariableName(param, node->module);

                Symbol symbol;
                symbol.node = nullptr;
                symbol.original_name = param;
                symbol.owner_level = state_ptr->level;
                symbol.field_index = state_ptr->CurrentLevel().locals.size();
                state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
            }

            Accept(body);
            DecreaseBlock();

            if (node->HasAttribute("inline")) {
                // Inline functions cannot be recursive, so we will declare
                // the symbol here to avoid recursive usage.
                Symbol symbol;
                symbol.node = node;
                symbol.original_name = node->name;
                symbol.is_const = true;
                symbol.owner_level = state_ptr->level;
                symbol.field_index = state_ptr->CurrentLevel().locals.size();
                state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
            }
        }
    }
}

void SemanticAnalyzer::Accept(AstFunctionExpression *node)
{
    std::string name = "unnamed";
    AstBlock *body = dynamic_cast<AstBlock*>(node->block.get());
    if (body) {
        if (body->children.empty()) {
            SourceLocation loc = body->location;

            InfoMsg(Msg_empty_function_body, loc, name);
            //InfoMsg(Msg_missing_final_return, node->location, name);

            // add return statement
            auto ret_value = std::unique_ptr<AstNull>(new AstNull(loc, node->module));
            auto ret_ast = std::unique_ptr<AstReturnStmt>(new AstReturnStmt(loc, node->module, std::move(ret_value)));

            body->AddChild(std::move(ret_ast));
        } else {
            bool has_return = false;
            size_t index = body->children.size() - 1;

            if (body->children[index]->type == Ast_type_return) {
                has_return = true;
            } else {
                while (index > 0 && body->children[index]->type == Ast_type_statement) {
                    if (body->children[index - 1]->type == Ast_type_return) {
                        has_return = true;
                        break;
                    } else if (body->children[index - 1]->type != Ast_type_statement) {
                        has_return = false;
                        break;
                    }
                    --index;
                }
            }

            if (!has_return) {
                SourceLocation location = body->children.back() ? body->children.back()->location : body->location;

                //InfoMsg(Msg_missing_final_return, node->location, name);

                // add return statement
                auto ret_value = std::unique_ptr<AstNull>(new AstNull(location, node->module));
                auto ret_ast = std::unique_ptr<AstReturnStmt>(new AstReturnStmt(location, node->module, std::move(ret_value)));

                body->AddChild(std::move(ret_ast));
            }
        }

        IncreaseBlock(LevelType::Level_function);

        // declare a variable for all parameters
        for (const std::string &param : node->arguments) {
            std::string var_name = state_ptr->MakeVariableName(param, node->module);

            Symbol symbol;
            symbol.node = node;
            symbol.original_name = param;
            symbol.owner_level = state_ptr->level;
            symbol.field_index = state_ptr->CurrentLevel().locals.size();
            state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
        }

        Accept(body);
        DecreaseBlock();
    }
}

void SemanticAnalyzer::Accept(AstFunctionCall *node)
{
    std::string var_name(state_ptr->MakeVariableName(node->name, node->module));
    SymbolQueryResult query = FindVariable(var_name, false);
    if (query) {
        if (query.symbol->is_alias) {
            node->is_alias = true;
            node->alias_to = query.symbol->node;
        }

        node->definition = query.symbol->node;

        IncrementUseCount(query.symbol->node);

        for (int i = node->arguments.size() - 1; i >= 0; i--) {
            // Push each argument onto the stack
            Accept(node->arguments[i].get());
        }
    } else {
        ErrorMsg(Msg_undeclared_identifier, node->location, node->name);
    }
}

void SemanticAnalyzer::Accept(AstClass *node)
{
}

void SemanticAnalyzer::Accept(AstObjectExpression *node)
{
    for (auto &&mem : node->members) {
        Accept(mem.second.get());
    }
}

void SemanticAnalyzer::Accept(AstEnum *node)
{
    // currently, the enum identifier is not created, only the members of it

    /*std::string var_name = state_ptr->MakeVariableName(node->name, node->module);
    if (FindVariable(var_name, true)) {
      ErrorMsg(msg_redeclared_identifier, node->location, node->name);
    } else if (state_ptr->FindModule(node->name, node->module)) {
      ErrorMsg(msg_identifier_is_module, node->location, node->name);
    } else {
      Symbol info;
      info.node = node;
      info.original_name = node->name;
      state_ptr->current_level().locals.push_back({ var_name, info });
    }*/
    for (auto &&it : node->members) {
        std::string var_name = state_ptr->MakeVariableName(it.first, it.second->module);
        if (FindVariable(var_name, true)) {
            ErrorMsg(Msg_redeclared_identifier, it.second->location, it.first);
        } else if (state_ptr->FindModule(it.first, it.second->module)) {
            ErrorMsg(Msg_identifier_is_module, it.second->location, it.first);
        } else {
            Symbol symbol;
            symbol.node = it.second.get();
            symbol.original_name = it.first;
            symbol.is_alias = true;
            symbol.is_const = true;
            symbol.owner_level = state_ptr->level;
            symbol.field_index = state_ptr->CurrentLevel().locals.size();
            state_ptr->CurrentLevel().locals.push_back({ var_name, symbol });
        }
    }
}

void SemanticAnalyzer::Accept(AstIfStmt *node)
{
    Accept(node->conditional.get());

    IncreaseBlock(LevelType::Level_condition);
    Accept(node->block.get());
    DecreaseBlock();

    if (node->else_statement) {
        IncreaseBlock(LevelType::Level_condition);
        Accept(node->else_statement.get());
        DecreaseBlock();
    }
}

void SemanticAnalyzer::Accept(AstPrintStmt *node)
{
    for (auto &&arg : node->arguments) {
        Accept(arg.get());
    }
}

void SemanticAnalyzer::Accept(AstReturnStmt *node)
{
    // The resulting value will get pushed onto the stack
    Accept(node->value.get());

    int start = state_ptr->level;
    LevelInfo *level = &state_ptr->levels[start];
    while (start >= compiler_global_level && level->type != LevelType::Level_function) {
        level = &state_ptr->levels[--start];
    }
}

void SemanticAnalyzer::Accept(AstForLoop *node)
{
    /*std::string var_name = state_ptr->MakeVariableName(node->identifier, node->module);

    // create the index variable
    Symbol info;
    info.node = nullptr;
    info.original_name = node->identifier;
    state_ptr->current_level().locals.push_back({ var_name, info });*/

    if (node->block != nullptr) {
        auto *block = dynamic_cast<AstBlock*>(node->block.get());
        if (block != nullptr && block->children.empty()) {
            InfoMsg(Msg_empty_statement_body, block->location);
        }
    }

    Accept(node->initializer.get());
    Accept(node->conditional.get());

    IncreaseBlock(LevelType::Level_loop);
    Accept(node->block.get());
    DecreaseBlock();

    Accept(node->afterthought.get());
}

void SemanticAnalyzer::Accept(AstWhileLoop *node)
{
    Accept(node->conditional.get());

    if (node->block != nullptr) {
        auto *block = dynamic_cast<AstBlock*>(node->block.get());
        if (block != nullptr && block->children.empty()) {
            InfoMsg(Msg_empty_statement_body, block->location);
        }
    }

    IncreaseBlock(LevelType::Level_loop);
    Accept(node->block.get());
    DecreaseBlock();
}

void SemanticAnalyzer::Accept(AstTryCatch *node)
{
    if (node->try_block != nullptr) {
        auto *block = dynamic_cast<AstBlock*>(node->try_block.get());
        if (block != nullptr && block->children.empty()) {
            InfoMsg(Msg_empty_statement_body, block->location);
        }
    }

    IncreaseBlock(LevelType::Level_default);
    Accept(node->try_block.get());
    DecreaseBlock();

    if (node->catch_block != nullptr) {
        auto *block = dynamic_cast<AstBlock*>(node->catch_block.get());
        if (block != nullptr && block->children.empty()) {
            InfoMsg(Msg_empty_statement_body, block->location);
        }
    }

    IncreaseBlock(LevelType::Level_default);
    Accept(node->exception_object.get());
    Accept(node->catch_block.get());
    DecreaseBlock();
}

void SemanticAnalyzer::Accept(AstRange *node)
{
}

void SemanticAnalyzer::IncrementUseCount(AstNode *ptr)
{
    if (state_ptr->use_counts.find(ptr) != state_ptr->use_counts.end()) {
        ++state_ptr->use_counts[ptr];
    } else {
        state_ptr->use_counts.insert({ ptr, 1 });
    }
}

SymbolQueryResult SemanticAnalyzer::FindVariable(const std::string &identifier, bool only_this_scope)
{
    SymbolQueryResult result;
    result.found = false;
    result.symbol = nullptr;

    // start at current level
    int start = state_ptr->level;
    while (start >= compiler_global_level) {
        auto &level = state_ptr->levels[start];
        /*auto it = std::find_if(level.locals.begin(), level.locals.end(),
            [&identifier](const std::pair<std::string, Symbol> &elt)
        {
            return elt.first == identifier;
        });

        if (it != level.locals.end()) {
            result.owner_level = start;
            result.field_index = 
        }*/

        for (size_t i = 0; i < level.locals.size(); i++) {
            const auto &key = level.locals[i].first;
            auto &value = level.locals[i].second;
            if (key == identifier) {
                // found the symbol
                result.found = true;
                result.symbol = &value;
                return result;
            }
        }

        if (only_this_scope) {
            break;
        }
        start--;
    }

    return result;
}

void SemanticAnalyzer::IncreaseBlock(LevelType type)
{
    LevelInfo level;
    level.type = type;
    state_ptr->levels[++state_ptr->level] = level;
}

void SemanticAnalyzer::DecreaseBlock()
{
    LevelInfo &level = state_ptr->CurrentLevel();
    for (auto &&local : level.locals) {
        if (local.second.node != nullptr) {
            auto usecount = state_ptr->use_counts[local.second.node];
            if (usecount == 0) {
                WarningMessage(Msg_unused_identifier, 
                    local.second.node->location, local.second.original_name);
            }
        }
    }
    state_ptr->levels[state_ptr->level--] = LevelInfo();
}
} // namespace avm