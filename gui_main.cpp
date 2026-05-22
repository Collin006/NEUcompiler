#include <QApplication>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include "compiler_core.h"

namespace {

QTextEdit* createOutputView() {
    auto* view = new QTextEdit();
    view->setReadOnly(true);
    view->setLineWrapMode(QTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return view;
}

QString toQString(const std::string& text) {
    return QString::fromUtf8(text.c_str());
}

QString formatOutputFile(const QString& label, const CompilerOutputFile& file) {
    if (file.path.empty()) {
        return label + ": 未生成";
    }
    if (file.written) {
        return label + ": " + QString::fromUtf8(file.path.c_str());
    }
    return label + ": 未生成";
}

} // namespace

class CompilerWindow final : public QMainWindow {
public:
    CompilerWindow() {
        setWindowTitle(QStringLiteral("NEUcompiler GUI"));

        auto* central = new QWidget();
        auto* mainLayout = new QVBoxLayout();
        auto* fileLayout = new QHBoxLayout();

        fileLayout->addWidget(new QLabel(QStringLiteral("源文件:")));

        fileEdit_ = new QLineEdit();
        fileEdit_->setPlaceholderText(QStringLiteral("选择待编译的源文件"));
        fileLayout->addWidget(fileEdit_);

        browseButton_ = new QPushButton(QStringLiteral("浏览"));
        fileLayout->addWidget(browseButton_);

        runButton_ = new QPushButton(QStringLiteral("运行编译"));
        fileLayout->addWidget(runButton_);

        mainLayout->addLayout(fileLayout);

        tabs_ = new QTabWidget();
        tokensView_ = createOutputView();
        expressionView_ = createOutputView();
        parseView_ = createOutputView();
        rawQuadView_ = createOutputView();
        optimizedQuadView_ = createOutputView();
        activeView_ = createOutputView();
        targetView_ = createOutputView();
        filesView_ = createOutputView();

        tabs_->addTab(tokensView_, QStringLiteral("词法分析"));
        tabs_->addTab(expressionView_, QStringLiteral("表达式分析表"));
        tabs_->addTab(parseView_, QStringLiteral("语法分析"));
        tabs_->addTab(rawQuadView_, QStringLiteral("四元式(原始)"));
        tabs_->addTab(optimizedQuadView_, QStringLiteral("四元式(优化)"));
        tabs_->addTab(activeView_, QStringLiteral("活跃信息"));
        tabs_->addTab(targetView_, QStringLiteral("目标代码"));
        tabs_->addTab(filesView_, QStringLiteral("输出文件"));

        mainLayout->addWidget(tabs_);
        central->setLayout(mainLayout);
        setCentralWidget(central);
        statusBar()->showMessage(QStringLiteral("就绪"));

        QObject::connect(browseButton_, &QPushButton::clicked, [this]() {
            chooseFile();
        });
        QObject::connect(runButton_, &QPushButton::clicked, [this]() {
            runCompile();
        });
    }

private:
    void chooseFile() {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择源文件"),
            QString(),
            QStringLiteral("Source Files (*.txt *.pas *.pascal);;All Files (*)"));
        if (!filePath.isEmpty()) {
            fileEdit_->setText(filePath);
        }
    }

    void runCompile() {
        QString filePath = fileEdit_->text().trimmed();
        if (filePath.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择源文件"));
            return;
        }

        statusBar()->showMessage(QStringLiteral("编译中..."));
        std::string sourcePath = filePath.toUtf8().constData();
        CompilerResult result = compileSourceFile(sourcePath);

        tokensView_->setText(toQString(result.outputs.tokens));
        expressionView_->setText(toQString(result.outputs.expressionAnalysis));
        parseView_->setText(toQString(result.outputs.parseLog));
        rawQuadView_->setText(toQString(result.outputs.rawQuadruples));
        optimizedQuadView_->setText(toQString(result.outputs.optimizedQuadruples));
        activeView_->setText(toQString(result.outputs.activeMark));
        targetView_->setText(toQString(result.outputs.targetCode));

        QStringList fileLines;
        fileLines << formatOutputFile(QStringLiteral("原始四元式"),
                                      result.files.rawQuadruples);
        fileLines << formatOutputFile(QStringLiteral("语法日志"),
                                      result.files.parseLog);
        fileLines << formatOutputFile(QStringLiteral("优化后四元式"),
                                      result.files.quadruples);
        fileLines << formatOutputFile(QStringLiteral("活跃信息标记"),
                                      result.files.activeMark);
        fileLines << formatOutputFile(QStringLiteral("目标代码"),
                                      result.files.targetCode);
        filesView_->setText(fileLines.join(QStringLiteral("\n")));

        if (result.success) {
            statusBar()->showMessage(QStringLiteral("编译完成"));
        } else if (result.errorIsParse) {
            statusBar()->showMessage(QStringLiteral("语法分析失败"));
        } else if (result.errorMessage.empty()) {
            statusBar()->showMessage(QStringLiteral("编译失败"));
        } else {
            statusBar()->showMessage(QStringLiteral("编译失败: ") +
                                     QString::fromUtf8(result.errorMessage.c_str()));
        }

        if (!result.errorMessage.empty()) {
            QString title = result.errorIsParse
                                ? QStringLiteral("语法分析错误")
                                : QStringLiteral("编译错误");
            QMessageBox::warning(this, title,
                                 QString::fromUtf8(result.errorMessage.c_str()));
        }
    }

    QLineEdit* fileEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTextEdit* tokensView_ = nullptr;
    QTextEdit* expressionView_ = nullptr;
    QTextEdit* parseView_ = nullptr;
    QTextEdit* rawQuadView_ = nullptr;
    QTextEdit* optimizedQuadView_ = nullptr;
    QTextEdit* activeView_ = nullptr;
    QTextEdit* targetView_ = nullptr;
    QTextEdit* filesView_ = nullptr;
};

int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);
    CompilerWindow window;
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
