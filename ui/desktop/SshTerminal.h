#pragma once
#include <QWidget>
#include <QByteArray>
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
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void setSendHandler(std::function<void(QByteArray)> fn) { send_ = std::move(fn); }
    void setResizeHandler(std::function<void(int,int)> fn) { resize_ = std::move(fn); }
protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool focusNextPrevChild(bool) override { return false; }
private:
    void resizeGrid();
    VTerm* vt_ = nullptr;
    VTermScreen* screen_ = nullptr;
    int rows_ = 24;
    int cols_ = 80;
    int cellW_ = 9;
    int cellH_ = 18;
    std::function<void(QByteArray)> send_;
    std::function<void(int,int)> resize_;
};
