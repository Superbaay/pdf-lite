#pragma once

// Comandos de edición (patrón Command) que usan text_edit, images y
// annotations. Todos son asíncronos: execute()/undo() encolan trabajo en el
// Engine y actualizan su estado cuando llega la respuesta.
//
// Mientras haya operaciones en vuelo (doc.pending_edits > 0) la interfaz
// bloquea deshacer/rehacer, así un comando nunca se revierte antes de saber,
// por ejemplo, qué ObjectId recibió el objeto que insertó.

#include "modules/context.hpp"

#include <string>
#include <utility>

namespace modules {

// Base: lleva la cuenta de operaciones pendientes y avisa de errores.
class AsyncCommand : public core::Command {
protected:
    explicit AsyncCommand(AppContext& ctx) : m_ctx{ctx} {}

    // Envuelve un callback: decrementa pending_edits y notifica errores.
    template <typename T, typename OnOk>
    auto track(int page, OnOk on_ok) {
        ++m_ctx.doc.pending_edits;
        const std::uint64_t generation = m_ctx.doc.generation;
        // Capturamos el contexto (que vive toda la app), no `this`: si el documento
        // se cerró, la pila de comandos se vació y este objeto ya no existe. Solo
        // tocamos el comando (on_ok) si la generación del documento no cambió.
        return
            [&ctx = m_ctx, page, generation, on_ok = std::move(on_ok)](pdf::Result<T> r) mutable {
                --ctx.doc.pending_edits;
                if (generation != ctx.doc.generation) {
                    return; // el documento cambió (se cerró o reabrió): ignorar
                }
                if (!r) {
                    ctx.notify("No se pudo completar la edición: " + r.error(), true);
                    return;
                }
                if constexpr (std::is_void_v<T>) {
                    on_ok();
                } else {
                    on_ok(std::move(*r));
                }
                ctx.page_edited(page);
            };
    }

    AppContext& m_ctx;
};

// Inserta texto o imagen. La primera ejecución crea el objeto; rehacer lo
// reinserta (mismo ObjectId) y deshacer lo quita sin destruirlo.
class InsertObjectCommand final : public AsyncCommand {
public:
    InsertObjectCommand(AppContext& ctx, int page, pdf::TextSpec text)
        : AsyncCommand{ctx}, m_page{page}, m_text{std::move(text)}, m_is_text{true} {}
    InsertObjectCommand(AppContext& ctx, int page, pdf::ImageSpec image)
        : AsyncCommand{ctx}, m_page{page}, m_image{std::move(image)}, m_is_text{false} {}

    void execute() override {
        if (m_id.valid()) {
            m_ctx.engine.attach(m_id, m_index,
                                track<pdf::ObjectInfo>(m_page, [](pdf::ObjectInfo) {}));
            return;
        }
        auto done =
            track<pdf::ObjectInfo>(m_page, [this](pdf::ObjectInfo info) { m_id = info.id; });
        if (m_is_text) {
            m_ctx.engine.insert_text(m_page, m_text, std::move(done));
        } else {
            // La imagen puede ser grande: tras insertarla ya no necesitamos los píxeles
            // (rehacer reutiliza el objeto PDF), así que los movemos.
            m_ctx.engine.insert_image(m_page, std::move(m_image), std::move(done));
        }
    }
    void undo() override {
        if (m_id.valid()) {
            m_ctx.engine.detach(m_id, track<int>(m_page, [this](int index) { m_index = index; }));
        }
    }
    [[nodiscard]] std::string description() const override {
        return m_is_text ? "insertar texto" : "insertar imagen";
    }

private:
    int m_page;
    pdf::TextSpec m_text;
    pdf::ImageSpec m_image;
    bool m_is_text;
    pdf::ObjectId m_id;
    int m_index = 0;
};

class DeleteObjectCommand final : public AsyncCommand {
public:
    DeleteObjectCommand(AppContext& ctx, pdf::ObjectId id) : AsyncCommand{ctx}, m_id{id} {}
    void execute() override {
        m_ctx.engine.detach(m_id, track<int>(m_id.page, [this](int index) { m_index = index; }));
    }
    void undo() override {
        m_ctx.engine.attach(m_id, m_index,
                            track<pdf::ObjectInfo>(m_id.page, [](pdf::ObjectInfo) {}));
    }
    [[nodiscard]] std::string description() const override { return "eliminar objeto"; }

private:
    pdf::ObjectId m_id;
    int m_index = 0;
};

class TransformObjectCommand final : public AsyncCommand {
public:
    TransformObjectCommand(AppContext& ctx, pdf::ObjectId id, pdf::Rect from, pdf::Rect to)
        : AsyncCommand{ctx}, m_id{id}, m_from{from}, m_to{to} {}
    void execute() override { apply(m_to); }
    void undo() override { apply(m_from); }
    [[nodiscard]] std::string description() const override { return "mover/redimensionar"; }

private:
    void apply(pdf::Rect r) {
        m_ctx.engine.set_bounds(m_id, r, track<pdf::ObjectInfo>(m_id.page, [](pdf::ObjectInfo) {}));
    }
    pdf::ObjectId m_id;
    pdf::Rect m_from;
    pdf::Rect m_to;
};

// Nivel 2: sustituye un objeto de texto existente por uno nuevo.
class ReplaceTextCommand final : public AsyncCommand {
public:
    ReplaceTextCommand(AppContext& ctx, pdf::ObjectId original, pdf::TextSpec spec)
        : AsyncCommand{ctx}, m_original{original}, m_spec{std::move(spec)} {}

    void execute() override {
        const int page = m_original.page;
        if (!m_replacement.valid()) {
            m_ctx.engine.replace_text(
                m_original, m_spec, track<pdf::ReplaceOutcome>(page, [this](pdf::ReplaceOutcome r) {
                    m_index = r.index;
                    m_replacement = r.replacement.id;
                }));
            return;
        }
        // Rehacer: quitar el original y volver a poner el reemplazo en su sitio.
        m_ctx.engine.detach(m_original, track<int>(page, [](int) {}));
        m_ctx.engine.attach(m_replacement, m_index,
                            track<pdf::ObjectInfo>(page, [](pdf::ObjectInfo) {}));
    }
    void undo() override {
        if (!m_replacement.valid()) {
            return;
        }
        const int page = m_original.page;
        m_ctx.engine.detach(m_replacement, track<int>(page, [](int) {}));
        m_ctx.engine.attach(m_original, m_index,
                            track<pdf::ObjectInfo>(page, [](pdf::ObjectInfo) {}));
    }
    [[nodiscard]] std::string description() const override { return "reemplazar texto"; }

private:
    pdf::ObjectId m_original;
    pdf::TextSpec m_spec;
    pdf::ObjectId m_replacement;
    int m_index = 0;
};

// Anotaciones: deshacer quita la ÚLTIMA añadida en la página. Como la pila es
// LIFO, el índice guardado sigue siendo correcto al deshacer.
class AddAnnotationCommand final : public AsyncCommand {
public:
    AddAnnotationCommand(AppContext& ctx, int page, pdf::AnnotSpec spec, std::string what)
        : AsyncCommand{ctx}, m_page{page}, m_spec{std::move(spec)}, m_what{std::move(what)} {}
    void execute() override {
        m_ctx.engine.add_annotation(m_page, m_spec,
                                    track<int>(m_page, [this](int index) { m_index = index; }));
    }
    void undo() override {
        if (m_index >= 0) {
            m_ctx.engine.remove_annotation(m_page, m_index, track<void>(m_page, [] {}));
        }
    }
    [[nodiscard]] std::string description() const override { return m_what; }

private:
    int m_page;
    pdf::AnnotSpec m_spec;
    std::string m_what;
    int m_index = -1;
};

} // namespace modules
