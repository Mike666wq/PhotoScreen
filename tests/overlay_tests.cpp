#include "captureoverlay.h"
#include <QApplication>
#include <QInputMethodEvent>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace photo;
class OverlayTests : public QObject {
    Q_OBJECT
private:
    static ScreenFrame frame(int width=600, int height=480) {
        ScreenFrame f; f.screenGeometry=QRect(0,0,width,height);
        f.image=QImage(width*2,height*2,QImage::Format_RGB32); f.image.fill(Qt::white);
        f.windowRects={QRect(20,20,200,120)}; return f;
    }
    static void selectArea(CaptureOverlay *overlay, const QPoint &start, const QPoint &end) {
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,end);
        QCoreApplication::processEvents();
    }
private slots:
    void releasePositionAndCopy() {
        QImage copied; int writes=0;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; ++writes; return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished); overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(210,110));
        QTest::keyClick(overlay,Qt::Key_Return);
        QCOMPARE(writes,1); QCOMPARE(copied.size(),QSize(400,200)); QCOMPARE(finished.count(),1);
    }
    void windowClickAndCancel() {
        int writes=0;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &,QString *) { ++writes; return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished); overlay->show(); QCoreApplication::processEvents();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(30,30));
        QTest::keyClick(overlay,Qt::Key_Escape);
        QCOMPARE(writes,0); QCOMPARE(finished.count(),1);
    }
    void arrowEndpointEditing() {
        QImage copied;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *arrow=overlay->findChild<QToolButton *>("tool_3"); QVERIFY(arrow); arrow->click();
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(340,150));
        auto *select=overlay->findChild<QToolButton *>("tool_0"); QVERIFY(select); select->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(190,100));
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(340,150));
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(430,120));
        QTest::keyClick(overlay,Qt::Key_Return);
        QVERIFY(!copied.isNull());
        const QColor body=copied.pixelColor(440,148); QVERIFY(body.red()>200 && body.green()<130 && body.blue()<130);
        QCOMPARE(copied.pixelColor(660,280),QColor(Qt::white));
    }
    void selectionHandleOverAnnotation() {
        QImage copied;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        selectArea(overlay,QPoint(10,10),QPoint(250,200));
        overlay->findChild<QToolButton *>("tool_0")->click();
        selectArea(overlay,QPoint(10,10),QPoint(30,30));
        QTest::keyClick(overlay,Qt::Key_Return);
        QCOMPARE(copied.size(),QSize(960,740));
    }
    void arrowThicknessCanBeEdited() {
        QImage copied;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width); width->setValue(3);
        auto *widthValue=overlay->findChild<QLabel *>("widthValue"); QVERIFY(widthValue);
        QCOMPARE(widthValue->text(),QStringLiteral("3"));
        selectArea(overlay,QPoint(50,100),QPoint(350,100));
        overlay->findChild<QToolButton *>("tool_0")->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));
        width->setValue(9);
        QCOMPARE(widthValue->text(),QStringLiteral("9"));
        QTest::keyClick(overlay,Qt::Key_Return);
        QVERIFY(!copied.isNull());
        const QColor widened=copied.pixelColor(500,190);
        QVERIFY(widened.red()>200 && widened.green()<130 && widened.blue()<130);
    }
    void textSlidersUpdateEditorAndExistingText() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_5")->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        auto *font=overlay->findChild<QSlider *>("fontControl"); QVERIFY(font);
        auto *weight=overlay->findChild<QSlider *>("fontWeightControl"); QVERIFY(weight);
        auto *fontValue=overlay->findChild<QLabel *>("fontValue"); QVERIFY(fontValue);
        auto *weightValue=overlay->findChild<QLabel *>("fontWeightValue"); QVERIFY(weightValue);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("Weight sample"));
        QApplication::sendEvent(editor,&input);
        font->setValue(44); weight->setValue(450);
        QCOMPARE(weightValue->text(),QStringLiteral("500"));
        weight->setValue(800);
        QCOMPARE(editor->font().pixelSize(),44);
        QCOMPARE(int(editor->font().weight()),800);
        QCOMPARE(fontValue->text(),QStringLiteral("44"));
        QCOMPARE(weightValue->text(),QStringLiteral("800"));
        QCOMPARE(int(editor->currentCharFormat().fontWeight()),800);
        QCOMPARE(editor->currentCharFormat().font().pixelSize(),44);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
        auto *select=overlay->findChild<QToolButton *>("tool_0"); QVERIFY(select); select->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,68));
        QCOMPARE(weight->value(),800);
        weight->setValue(500);
        auto *undo=overlay->findChild<QToolButton *>("undoButton"); QVERIFY(undo); undo->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,68));
        QCOMPARE(weight->value(),800);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void textInputAndFailureRetry() {
        QImage copied; int attempts=0;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *error) {
            if (++attempts==1) { *error=QStringLiteral("test failure"); return false; }
            copied=image; return true;
        });
        QSignalSpy finished(overlay,&CaptureOverlay::finished); overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *text=overlay->findChild<QToolButton *>("tool_5"); QVERIFY(text); text->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("测试文字")); QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Return);
        QInputMethodEvent input2; input2.setCommitString(QStringLiteral("第二行")); QApplication::sendEvent(editor,&input2);
        QCOMPARE(editor->toPlainText(),QStringLiteral("测试文字\n第二行"));
        auto *done=overlay->findChild<QToolButton *>("doneButton"); QVERIFY(done);
        QTimer::singleShot(0,[] { for (QWidget *widget : QApplication::topLevelWidgets()) if (auto *box=qobject_cast<QMessageBox *>(widget)) box->accept(); });
        done->click(); QCOMPARE(attempts,1); QCOMPARE(finished.count(),0);
        done->click(); QCOMPARE(attempts,2); QCOMPARE(finished.count(),1); QVERIFY(!copied.isNull());
        bool hasInk=false;
        for (int y=70;y<180 && !hasInk;++y) for(int x=50;x<450;++x) {
            const QColor c=copied.pixelColor(x,y); if (c.red()>200 && c.green()<150 && c.blue()<150) { hasInk=true; break; }
        }
        QVERIFY(hasInk);
    }
    void narrowToolbarKeepsActionsVisible() {
        auto *overlay=new CaptureOverlay(frame(320,260),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents(); selectArea(overlay,QPoint(10,10),QPoint(300,220));
        auto *done=overlay->findChild<QToolButton *>("doneButton"); QVERIFY(done); QVERIFY(done->isVisible());
        const QRect visible(done->mapTo(overlay,QPoint(0,0)),done->size());
        QVERIFY(overlay->rect().contains(visible));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void styleAdjustmentIsSingleUndoStep() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();      // 箭头
        selectArea(overlay,QPoint(50,100),QPoint(350,100));        // 画一个箭头
        overlay->findChild<QToolButton *>("tool_0")->click();      // 选择工具
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));  // 选中箭头
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        QCOMPARE(overlay->annotations().size(),1);
        width->setValue(3);
        // 拖动前的基准宽度，撤销后必须回到这里。
        const qreal widthBeforeDrag=overlay->annotations().first().width;
        const int stepsBefore=undoSteps(overlay);
        // 模拟一次拖动：按下后连续变值，最后释放。整段拖动只产生一步撤销。
        emit width->sliderPressed();
        for (int v=5;v<=12;++v) width->setValue(v);
        emit width->sliderReleased();
        QCOMPARE(undoSteps(overlay),stepsBefore+1);
        QCOMPARE(overlay->annotations().first().width,12.0);
        // 撤销一次应回到拖动前的状态，而不是只回退一个数值。
        overlay->findChild<QToolButton *>("undoButton")->click();
        QCOMPARE(undoSteps(overlay),stepsBefore);
        QCOMPARE(overlay->annotations().first().width,widthBeforeDrag);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void keyboardStyleChangeIsSingleUndoStep() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        selectArea(overlay,QPoint(50,100),QPoint(350,100));
        overlay->findChild<QToolButton *>("tool_0")->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        QCOMPARE(overlay->annotations().size(),1);
        width->setValue(3);
        const qreal widthBeforeChange=overlay->annotations().first().width;
        const int stepsBefore=undoSteps(overlay);
        // 键盘方向键调整：不经过 sliderPressed，每次调整仍应各自可撤销。
        width->setValue(6);
        QCOMPARE(undoSteps(overlay),stepsBefore+1);
        QCOMPARE(overlay->annotations().first().width,6.0);
        overlay->findChild<QToolButton *>("undoButton")->click();
        QCOMPARE(undoSteps(overlay),stepsBefore);
        QCOMPARE(overlay->annotations().first().width,widthBeforeChange);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void undoStillWorksAfterInterruptedDrag() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        selectArea(overlay,QPoint(50,100),QPoint(350,100));
        overlay->findChild<QToolButton *>("tool_0")->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        width->setValue(3);
        // 模拟滑块拖动中途失去 grab：按下后不释放，直接切换工具。
        emit width->sliderPressed();
        width->setValue(9);
        QVERIFY(overlay->styleEditActive());
        overlay->findChild<QToolButton *>("tool_3")->click();
        // 会话必须已提交，不能残留未闭合状态；撤销必须仍然可用。
        QVERIFY(!overlay->styleEditActive());
        auto *undo=overlay->findChild<QToolButton *>("undoButton"); QVERIFY(undo);
        QVERIFY(undo->isEnabled());
        const int steps=undoSteps(overlay);
        undo->click();
        QCOMPARE(undoSteps(overlay),steps-1);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void editingExistingTextKeepsItsColor() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_5")->click();      // 文字
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        const quintptr oldEditor=quintptr(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("彩色文字"));
        QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);   // 提交
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().size(),1);
        const QColor original=overlay->annotations().first().color;
        // 切回选择工具后双击已有文字，进入编辑态。
        overlay->findChild<QToolButton *>("tool_0")->click();
        QTest::mouseDClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,60));
        // 上一个编辑器可能还没被回收，取最新的那个。
        const auto editors=overlay->findChildren<QTextEdit *>("annotationTextEditor"); QVERIFY(!editors.isEmpty());
        auto *editor2=editors.last();
        QVERIFY(quintptr(editor2)!=oldEditor);
        // 编辑器预览必须是该标注自身的颜色，而不是工具栏当前色。
        QCOMPARE(editor2->currentCharFormat().foreground().color().name(),original.name());
        // 编辑过程中调整字号，颜色不得改变。
        auto *font=overlay->findChild<QSlider *>("fontControl"); QVERIFY(font);
        font->setValue(44);
        QCOMPARE(editor2->currentCharFormat().foreground().color().name(),original.name());
        QTest::keyClick(editor2,Qt::Key_Return,Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().first().color.name(),original.name());
        QCOMPARE(overlay->annotations().first().fontSize,44);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void explicitTextColorChangeMatchesPreview() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_5")->click();      // 文字
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("换色文字"));
        QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);   // 提交
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().size(),1);
        overlay->findChild<QToolButton *>("tool_0")->click();
        QTest::mouseDClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,60));
        // 上一个编辑器可能还没被回收，取最新的那个。
        const auto editors=overlay->findChildren<QTextEdit *>("annotationTextEditor"); QVERIFY(!editors.isEmpty());
        auto *editor2=editors.last();
        // 显式换色：编辑器预览必须立刻更新。
        overlay->applyChosenColor(QColor("#00b050"));
        QCOMPARE(editor2->currentCharFormat().foreground().color().name(),QStringLiteral("#00b050"));
        // 提交结果必须与预览一致。
        QTest::keyClick(editor2,Qt::Key_Return,Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().first().color.name(),QStringLiteral("#00b050"));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
private:
    static int undoSteps(CaptureOverlay *overlay) { return overlay->undoDepth(); }
};
QTEST_MAIN(OverlayTests)
#include "overlay_tests.moc"
