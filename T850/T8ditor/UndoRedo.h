/*********************************************************
 * T8ditor — Undo/redo command stack.
 *
 * Implements the Command Pattern for reversible editor
 * actions. Each action stores enough state to Apply and
 * Undo itself. The stack supports Ctrl+Z / Ctrl+Shift+Z.
 *********************************************************/

#ifndef T8DITOR_UNDO_REDO_H
#define T8DITOR_UNDO_REDO_H

#include <utils/xMaths.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace t8ditor {

// ── Base command ─────────────────────────────────────

class UndoCommand {
public:
  virtual ~UndoCommand() = default;
  virtual void Apply() = 0;
  virtual void Undo()  = 0;
  virtual const char* Description() const = 0;
};

// ── Transform command ────────────────────────────────
// Stores old and new T/R/S for one object.

struct TransformState {
  XVECTOR3 position;
  XVECTOR3 eulerRad;
  XVECTOR3 scale;
};

class TransformCommand : public UndoCommand {
public:
  // `setFn` is called with the state to apply (captures a reference
  // to the object's wireframe T/R/S).
  using SetFn = std::function<void(const TransformState&)>;

  TransformCommand(int objectIdx, const TransformState& before,
                   const TransformState& after, SetFn setFn)
    : m_objectIdx(objectIdx), m_before(before), m_after(after), m_setFn(setFn) {}

  void Apply() override { m_setFn(m_after); }
  void Undo()  override { m_setFn(m_before); }
  const char* Description() const override { return "Transform"; }

private:
  int            m_objectIdx;
  TransformState m_before;
  TransformState m_after;
  SetFn          m_setFn;
};

// ── Undo stack ───────────────────────────────────────

class UndoStack {
public:
  void Execute(std::unique_ptr<UndoCommand> cmd) {
    cmd->Apply();
    // Trim any redo history
    m_commands.resize(m_current);
    m_commands.push_back(std::move(cmd));
    m_current = (int)m_commands.size();
  }

  // Push a command that was already applied (e.g., ImGuizmo did the work).
  void Push(std::unique_ptr<UndoCommand> cmd) {
    m_commands.resize(m_current);
    m_commands.push_back(std::move(cmd));
    m_current = (int)m_commands.size();
  }

  bool CanUndo() const { return m_current > 0; }
  bool CanRedo() const { return m_current < (int)m_commands.size(); }

  void Undo() {
    if (!CanUndo()) return;
    --m_current;
    m_commands[m_current]->Undo();
  }

  void Redo() {
    if (!CanRedo()) return;
    m_commands[m_current]->Apply();
    ++m_current;
  }

  void Clear() {
    m_commands.clear();
    m_current = 0;
  }

  int UndoCount() const { return m_current; }
  int RedoCount() const { return (int)m_commands.size() - m_current; }

private:
  std::vector<std::unique_ptr<UndoCommand>> m_commands;
  int m_current = 0;  // index of the next command to push (= undo depth)
};

} // namespace t8ditor

#endif // T8DITOR_UNDO_REDO_H
