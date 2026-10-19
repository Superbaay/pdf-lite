#include "core/command_stack.hpp"

namespace core {

void CommandStack::execute(std::unique_ptr<Command> command) {
    command->execute();
    push_executed(std::move(command));
}

void CommandStack::push_executed(std::unique_ptr<Command> command) {
    m_undo.push_back(std::move(command));
    m_redo.clear();
    ++m_revision;
    trim();
}

bool CommandStack::undo() {
    if (m_undo.empty()) {
        return false;
    }
    auto command = std::move(m_undo.back());
    m_undo.pop_back();
    command->undo();
    m_redo.push_back(std::move(command));
    ++m_revision;
    return true;
}

bool CommandStack::redo() {
    if (m_redo.empty()) {
        return false;
    }
    auto command = std::move(m_redo.back());
    m_redo.pop_back();
    command->execute();
    m_undo.push_back(std::move(command));
    ++m_revision;
    return true;
}

void CommandStack::clear() {
    m_undo.clear();
    m_redo.clear();
    ++m_revision;
}

std::string CommandStack::undo_description() const {
    return m_undo.empty() ? std::string{} : m_undo.back()->description();
}

std::string CommandStack::redo_description() const {
    return m_redo.empty() ? std::string{} : m_redo.back()->description();
}

void CommandStack::trim() {
    if (m_undo.size() > m_max_depth) {
        m_undo.erase(m_undo.begin(),
                     m_undo.begin() + static_cast<std::ptrdiff_t>(m_undo.size() - m_max_depth));
    }
}

} // namespace core
