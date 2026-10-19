#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace core {

// Patrón Command: cada acción de edición es un objeto que sabe aplicarse
// (execute) y revertirse (undo). La pila guarda los comandos ejecutados; deshacer
// llama a undo() del último y lo mueve a la pila de rehacer.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    // Texto corto para la interfaz: "Deshacer: insertar texto".
    [[nodiscard]] virtual std::string description() const = 0;
};

class CommandStack {
public:
    explicit CommandStack(std::size_t max_depth = 200) : m_max_depth{max_depth} {}

    // Ejecuta el comando y lo apila. Borra el historial de rehacer: después de
    // una acción nueva, "rehacer" ya no tiene sentido.
    void execute(std::unique_ptr<Command> command);

    // Apila un comando que ya se aplicó por otra vía (p. ej. el usuario arrastró
    // un objeto y el cambio se hizo en vivo).
    void push_executed(std::unique_ptr<Command> command);

    bool undo();
    bool redo();
    void clear();

    [[nodiscard]] bool can_undo() const { return !m_undo.empty(); }
    [[nodiscard]] bool can_redo() const { return !m_redo.empty(); }
    [[nodiscard]] std::string undo_description() const;
    [[nodiscard]] std::string redo_description() const;
    [[nodiscard]] std::size_t undo_size() const { return m_undo.size(); }

    // Contador que cambia con cada execute/undo/redo: sirve para saber si el
    // documento difiere del último guardado (comparando con el valor guardado).
    [[nodiscard]] std::size_t revision() const { return m_revision; }

private:
    void trim();

    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    std::size_t m_max_depth;
    std::size_t m_revision = 0;
};

} // namespace core
