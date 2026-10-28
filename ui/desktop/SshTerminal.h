#pragma once
#include <QWidget>
#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QVector>
#include <functional>
#include <utility>

extern "C" {
#include <vterm.h>
}

class SshTerminal final : public QWidget {
public:
    explicit SshTerminal(QWidget* parent = nullptr);
    ~SshTerminal() override;
    void receive(const QByteArray& bytes);
    void clearSession();
    bool copySelectionToClipboard() const;
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void setSendHandler(std::function<void(QByteArray)> fn) { send_ = std::move(fn); }
    void setResizeHandler(std::function<void(int,int)> fn) { resize_ = std::move(fn); }
protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    bool focusNextPrevChild(bool) override { return false; }
private:
    struct CellPosition { int row = 0; int col = 0; };
    static int pushScrollback(int cols, const VTermScreenCell* cells, void* user);
    static int popScrollback(int cols, VTermScreenCell* cells, void* user);
    void resizeGrid();
    CellPosition positionAt(const QPoint& point) const;
    bool cellAt(int logicalRow, int col, VTermScreenCell* cell) const;
    QString selectionText() const;
    QString cellText(const VTermScreenCell& cell) const;
    void clearSelection();
    VTerm* vt_ = nullptr;
    VTermScreen* screen_ = nullptr;
    // Bounded encrypted-session output history: memory-only and never persisted.
    QVector<QVector<VTermScreenCell>> history_;
    static constexpr int kHistoryLimit = 3000;
    static constexpr int kHistoryWidthLimit = 1024;
    int scrollOffset_ = 0;
    bool selecting_ = false;
    bool hasSelection_ = false;
    CellPosition selectionAnchor_;
    CellPosition selectionEnd_;
    int rows_ = 24;
    int cols_ = 80;
    int cellW_ = 9;
    int cellH_ = 18;
    std::function<void(QByteArray)> send_;
    std::function<void(int,int)> resize_;
};
