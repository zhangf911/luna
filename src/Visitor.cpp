#include "Visitor.h"
#include "State.h"
#include "Function.h"
#include <vector>
#include <utility>
#include <assert.h>

namespace luna
{
    class NameScope;

    struct ScopeName
    {
        // name
        String *name_;
        // register index in function
        int register_;

        ScopeName() : name_(nullptr), register_(0) { }
        ScopeName(String *name, int reg) : name_(name), register_(reg) { }
    };

    struct ScopeNameList
    {
        std::vector<ScopeName> name_list_;
        NameScope *current_scope_;

        ScopeNameList() : current_scope_(nullptr) { }
    };

    class NameScope
    {
    public:
        explicit NameScope(ScopeNameList &name_list, Function *owner = nullptr)
            : name_list_(&name_list),
              previous_(name_list.current_scope_),
              start_(name_list.name_list_.size()),
              owner_(nullptr)
        {
            name_list_->current_scope_ = this;

            if (owner)
                owner_ = owner;
            else
                owner_ = previous_->owner_;
        }

        ~NameScope()
        {
            name_list_->name_list_.resize(start_);
            name_list_->current_scope_ = previous_;
        }

        // Get current scope level name by current name level index
        const ScopeName * GetScopeName(std::size_t index) const
        {
            if (start_ + index < name_list_->name_list_.size())
                return &name_list_->name_list_[start_ + index];
            return nullptr;
        }

        // Add name to scope if the name is not existed before,
        // '*reg' store the 'name' register if return false.
        bool AddScopeName(String *name, int *reg)
        {
            assert(reg);
            if (!IsBelongsToScope(name, reg))
            {
                name_list_->name_list_.push_back(ScopeName(name, *reg));
                return true;
            }

            return false;
        }

        // Get previous ScopeNameLevel
        NameScope * GetPrevious() const
        {
            return previous_;
        }

        // Is name in this scope, if 'name' exist, then '*reg' store the register
        bool IsBelongsToScope(const String *name, int *reg = nullptr) const
        {
            std::size_t end = name_list_->name_list_.size();
            for (std::size_t i = start_; i < end; ++i)
            {
                if (name_list_->name_list_[i].name_ == name)
                {
                    if (reg) *reg = name_list_->name_list_[i].register_;
                    return true;
                }
            }
            return false;
        }

        // Get the NameScope which the name belongs to
        std::pair<const NameScope *,
                  const Function *> GetBlongsToScope(const String *name) const
        {
            const NameScope *current = this;
            while (current)
            {
                if (current->IsBelongsToScope(name))
                    break;
                else
                    current = current->previous_;
            }

            const Function *func = current ? current->owner_ : nullptr;
            return std::make_pair(current, func);
        }

    private:
        // scope name list
        ScopeNameList *name_list_;

        // previous scope
        NameScope *previous_;

        // start index in name_list_
        std::size_t start_;

        // scope owner function
        Function *owner_;
    };

    class CodeGenerateVisitor : public Visitor
    {
    public:
        explicit CodeGenerateVisitor(State *state);

        virtual void Visit(Chunk *);
        virtual void Visit(Block *);
        virtual void Visit(ReturnStatement *);
        virtual void Visit(BreakStatement *);
        virtual void Visit(DoStatement *);
        virtual void Visit(WhileStatement *);
        virtual void Visit(RepeatStatement *);
        virtual void Visit(IfStatement *);
        virtual void Visit(ElseIfStatement *);
        virtual void Visit(ElseStatement *);
        virtual void Visit(NumericForStatement *);
        virtual void Visit(GenericForStatement *);
        virtual void Visit(FunctionStatement *);
        virtual void Visit(FunctionName *);
        virtual void Visit(LocalFunctionStatement *);
        virtual void Visit(LocalNameListStatement *);
        virtual void Visit(AssignmentStatement *);
        virtual void Visit(VarList *);
        virtual void Visit(Terminator *);
        virtual void Visit(BinaryExpression *);
        virtual void Visit(UnaryExpression *);
        virtual void Visit(FunctionBody *);
        virtual void Visit(ParamList *);
        virtual void Visit(NameList *);
        virtual void Visit(TableDefine *);
        virtual void Visit(TableIndexField *);
        virtual void Visit(TableNameField *);
        virtual void Visit(TableArrayField *);
        virtual void Visit(IndexAccessor *);
        virtual void Visit(MemberAccessor *);
        virtual void Visit(NormalFuncCall *);
        virtual void Visit(MemberFuncCall *);
        virtual void Visit(ExpressionList *);

    private:
        State *state_;
        ScopeNameList scope_name_list_;

        struct NameReg
        {
            int register_;
            const TokenDetail *token_;

            NameReg(int reg, const TokenDetail &t)
                : register_(reg), token_(&t) { }
        };

        std::vector<NameReg> names_register_;

        // current function
        Function *func_;
    };

    CodeGenerateVisitor::CodeGenerateVisitor(State *state)
        : state_(state),
          func_(nullptr)
    {
    }

    void CodeGenerateVisitor::Visit(Chunk *chunk)
    {
        auto func = state_->NewFunction();
        func->SetBaseInfo(chunk->module_, 0);
        func->SetSuperior(func_);
        func_ = func;

        chunk->block_->Accept(this);
    }

    void CodeGenerateVisitor::Visit(Block *block)
    {
        NameScope current(scope_name_list_, func_);
        int reg = func_->GetNextRegister();

        // Visit all statements
        for (auto &s : block->statements_)
            s->Accept(this);

        // Visit return statement if exist
        if (block->return_stmt_)
            block->return_stmt_->Accept(this);

        // Restore register
        func_->SetNextRegister(reg);
        func_->AddInstruction(Instruction::ACode(OpType_SetTop, reg), 0);
    }

    void CodeGenerateVisitor::Visit(LocalNameListStatement *local_name)
    {
        // Visit local names
        local_name->name_list_->Accept(this);

        int reg = func_->GetNextRegister();
        int reg_count = func_->GetRegisterCount();

        // Visit exp list
        if (local_name->exp_list_)
            local_name->exp_list_->Accept(this);

        int names = names_register_.size();
        func_->SetRegisterCount(reg_count + names);

        // Set local name init value
        int exp_reg = reg;
        for (int i = 0; i < names; ++i, ++exp_reg)
        {
            int name_reg = names_register_[i].register_;
            func_->AddInstruction(Instruction::ABCode(OpType_Move,
                                                      name_reg, exp_reg),
                                  names_register_[i].token_->line_);
        }

        names_register_.clear();

        // Restore register
        func_->SetNextRegister(reg);
        func_->AddInstruction(Instruction::ACode(OpType_SetTop, reg), 0);
    }

    void CodeGenerateVisitor::Visit(Terminator *term)
    {
        const TokenDetail &t = term->token_;

        int index = 0;
        if (t.token_ == Token_Number)
            index = func_->AddConstNumber(t.number_);
        else if (t.token_ == Token_String)
            index = func_->AddConstString(t.str_);
        else
            assert(!"maybe miss some term type");

        int reg = func_->AllocaNextRegister();
        func_->AddInstruction(Instruction::ABCode(OpType_LoadConst,
                                                  reg, index), t.line_);
    }

    void CodeGenerateVisitor::Visit(NameList *name_list)
    {
        // Add all names to local scope
        for (auto &n : name_list->names_)
        {
            assert(n.token_ == Token_Id);
            int reg = func_->GetNextRegister();
            if (scope_name_list_.current_scope_->AddScopeName(n.str_, &reg))
                func_->AllocaNextRegister();

            // Add name register, used by other Visit function to generate code
            names_register_.push_back(NameReg(reg, n));
        }
    }

    void CodeGenerateVisitor::Visit(NormalFuncCall *func_call)
    {
        int reg = func_->GetNextRegister();

        // Load function
        func_call->caller_->Accept(this);

        // Set the first register for args
        func_->SetNextRegister(reg + 1);
        func_->AddInstruction(Instruction::ACode(OpType_SetTop, reg + 1), 0);

        // Prepare args
        if (func_call->args_)
            func_call->args_->Accept(this);

        func_->AddInstruction(Instruction::ACode(OpType_Call, reg), 0);
    }

    void CodeGenerateVisitor::Visit(ExpressionList *exp_list)
    {
        int reg = func_->GetNextRegister();

        // Visit each expression
        for (auto &exp : exp_list->exp_list_)
        {
            // Set start register for each expression
            func_->SetNextRegister(reg);
            func_->AddInstruction(Instruction::ACode(OpType_SetTop, reg), 0);
            exp->Accept(this);
            ++reg;
        }
    }

    std::unique_ptr<Visitor> GenerateVisitor(State *state)
    {
        return std::unique_ptr<Visitor>(new CodeGenerateVisitor(state));
    }
} // namespace luna
