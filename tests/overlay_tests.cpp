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
    static QImage renderSelectedTextWeight(int finalWeight) {
        QImage copied;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *textTool=overlay->findChild<QToolButton *>("tool_5");
        auto *selectTool=overlay->findChild<QToolButton *>("tool_0");
        if (!textTool || !selectTool) return {};
        textTool->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor");
        auto *font=overlay->findChild<QSlider *>("fontControl");
        auto *weight=overlay->findChild<QSlider *>("fontWeightControl");
        if (!editor || !font || !weight) return {};
        font->setValue(44); weight->setValue(800);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("Weight"));
        QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
        selectTool->click();
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,68));
        weight->setValue(finalWeight);
        QTest::keyClick(overlay,Qt::Key_Return);
        return copied;
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
        font->setValue(44); weight->setValue(800);
        QCOMPARE(editor->font().pixelSize(),44);
        QCOMPARE(int(editor->font().weight()),800);
        QCOMPARE(fontValue->text(),QStringLiteral("44"));
        QCOMPARE(weightValue->text(),QStringLiteral("800"));
        QTest::keyClick(overlay,Qt::Key_Escape);

        const QImage light=renderSelectedTextWeight(100);
        const QImage heavy=renderSelectedTextWeight(900);
        QVERIFY(!light.isNull()); QVERIFY(!heavy.isNull()); QVERIFY(light!=heavy);
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
};
QTEST_MAIN(OverlayTests)
#include "overlay_tests.moc"
