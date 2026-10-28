#include "captureoverlay.h"
#include "core/interaction.h"
#include <QApplication>
#include <QFrame>
#include <QInputMethodEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QtTest>

using namespace photo;
class OverlayTests : public QObject {
    Q_OBJECT
private:
    static ScreenFrame frame(int width=600, int height=480) {
        // 单屏场景：逻辑 600×480，物理像素 1200×960（模拟 2x 屏）。
        ScreenFrame f;
        ScreenImage screen;
        screen.geometry=QRect(0,0,width,height);
        screen.image=QImage(width*2,height*2,QImage::Format_RGB32); screen.image.fill(Qt::white);
        f.screens={screen};
        f.virtualGeometry=screen.geometry;
        f.windowRects={QRect(20,20,200,120)};
        return f;
    }
    // 双屏场景：左屏 2x、右屏 1x，虚拟桌面横跨两屏。
    static ScreenFrame dualFrame() {
        ScreenFrame f;
        ScreenImage left;
        left.geometry=QRect(0,0,600,480); left.image=QImage(1200,960,QImage::Format_RGB32); left.image.fill(Qt::white);
        ScreenImage right;
        right.geometry=QRect(600,0,600,480); right.image=QImage(600,480,QImage::Format_RGB32); right.image.fill(Qt::white);
        f.screens={left,right};
        f.virtualGeometry=QRect(0,0,1200,480);
        f.windowRects={QRect(20,20,200,120)};
        return f;
    }
    static void selectArea(CaptureOverlay *overlay, const QPoint &start, const QPoint &end) {
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,end);
        QCoreApplication::processEvents();
    }
private slots:
    void initTestCase() {
        // 与 src/main.cpp 保持一致：设置组织名与应用名之后，QSettings 的落盘位置
        // 在 Windows 注册表与 macOS plist 上才一致，测试结果不会随平台漂移。
        QCoreApplication::setOrganizationName(QStringLiteral("PhotoScreenTests"));
        QCoreApplication::setApplicationName(QStringLiteral("OverlayTests"));
    }
    void init() { QSettings().clear(); }
    void cleanup() {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *overlay=qobject_cast<CaptureOverlay *>(widget)) overlay->close();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QSettings().clear();
    }
    void continuousDrawingKeepsSelectionForStyleAndUndo() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *arrow=overlay->findChild<QToolButton *>("tool_3"); QVERIFY(arrow);
        QTest::mouseClick(arrow,Qt::LeftButton);
        selectArea(overlay,QPoint(50,80),QPoint(250,100));
        selectArea(overlay,QPoint(50,160),QPoint(250,180));
        QCOMPARE(overlay->annotations().size(),2);
        const AnnotationId id=overlay->annotations().last().id;
        QVERIFY(arrow->isChecked());
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        width->setValue(8);
        QCOMPARE(overlay->annotations().last().width,8.0);
        QCOMPARE(overlay->annotations().first().width,3.0);
        QTest::mouseClick(overlay->findChild<QToolButton *>("undoButton"),Qt::LeftButton);
        QCOMPARE(overlay->annotations().last().id,id);
        QCOMPARE(width->value(),3);
        width->setValue(6);
        QCOMPARE(overlay->annotations().last().width,6.0);
    }
    void closingFlushesPreferencesAndEndsOnce() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show();
        QSignalSpy finished(overlay,&CaptureOverlay::finished);
        overlay->findChild<QSlider *>("widthControl")->setValue(9);
        overlay->close();
        QCOMPARE(finished.count(),1);
        QCOMPARE(QSettings().value("annotation/tools/rectangle/width").toInt(),9);
        QVERIFY(overlay->annotations().isEmpty());
    }
    void toolsRememberIndependentStylesAndSelectionDoesNotWriteDefaults() {
        QSettings settings;
        settings.setValue("annotation/width",5);
        settings.setValue("annotation/color",QStringLiteral("#112233"));
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        QCOMPARE(width->value(),5); width->setValue(12); overlay->applyChosenColor(QColor("#ff3b3b"));
        selectArea(overlay,QPoint(50,80),QPoint(300,100));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_1"),Qt::LeftButton);
        QCOMPARE(width->value(),5); width->setValue(2); overlay->applyChosenColor(QColor("#0a84ff"));
        selectArea(overlay,QPoint(50,160),QPoint(300,240));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        QCOMPARE(width->value(),12);
        QTest::qWait(450);
        QTRY_COMPARE_WITH_TIMEOUT(overlay->persistedLineWidth(),12,2000);
        auto toolSettings=[] {
            QSettings settings;
            settings.beginGroup(QStringLiteral("annotation/tools"));
            QVariantMap values;
            for (const QString &key : settings.allKeys()) values.insert(key,settings.value(key));
            return values;
        };
        const QVariantMap preferencesBefore=toolSettings();
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_0"),Qt::LeftButton);
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(50,200));
        QCOMPARE(width->value(),2);
        QTest::qWait(450);
        QCOMPARE(toolSettings(),preferencesBefore);
        QCOMPARE(settings.value("annotation/tools/arrow/width").toInt(),12);
        QCOMPARE(settings.value("annotation/tools/rectangle/width").toInt(),2);
        QCOMPARE(settings.value("annotation/tools/arrow/color").toString(),QStringLiteral("#ff3b3b"));
        QCOMPARE(settings.value("annotation/tools/rectangle/color").toString(),QStringLiteral("#0a84ff"));
        overlay->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto *again=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        again->show(); selectArea(again,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(again->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        QCOMPARE(again->findChild<QSlider *>("widthControl")->value(),12);
    }
    void readingLegacyPreferencesDoesNotCreateToolSettings() {
        QSettings settings;
        settings.setValue("annotation/color",QStringLiteral("#123456")); settings.setValue("annotation/width",7);
        const QStringList before=settings.allKeys();
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        QCOMPARE(overlay->findChild<QSlider *>("widthControl")->value(),7);
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        overlay->close();
        QCOMPARE(settings.allKeys(),before);
    }
    void colorSlidersReachWhiteBlackAndGroupUndo() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        overlay->applyChosenColor(QColor("#ff0000"));
        selectArea(overlay,QPoint(50,80),QPoint(300,100));
        QTest::mouseClick(overlay->findChild<QToolButton *>("colorButton"),Qt::LeftButton);
        QTest::mouseClick(overlay->findChild<QToolButton *>("advancedColorButton"),Qt::LeftButton);
        auto *saturation=overlay->findChild<QSlider *>("saturationControl"); QVERIFY(saturation);
        auto *value=overlay->findChild<QSlider *>("valueControl"); QVERIFY(value);
        const int depth=overlay->undoDepth();
        QTest::mousePress(saturation,Qt::LeftButton,Qt::NoModifier,QPoint(sliderHandleCenterX(saturation),saturation->height()/2));
        QTest::mouseMove(saturation,QPoint(saturation->width()/2,saturation->height()/2));
        QTest::mouseMove(saturation,QPoint(7,saturation->height()/2));
        QTest::mouseRelease(saturation,Qt::LeftButton,Qt::NoModifier,QPoint(7,saturation->height()/2));
        QCOMPARE(saturation->value(),0);
        QCOMPARE(overlay->annotations().first().color.name(),QStringLiteral("#ffffff"));
        QCOMPARE(overlay->undoDepth(),depth+1);
        QTest::mouseClick(overlay->findChild<QToolButton *>("undoButton"),Qt::LeftButton);
        QCOMPARE(overlay->annotations().first().color.name(),QStringLiteral("#ff0000"));
        QTest::mousePress(value,Qt::LeftButton,Qt::NoModifier,QPoint(sliderHandleCenterX(value),value->height()/2));
        QTest::mouseRelease(value,Qt::LeftButton,Qt::NoModifier,QPoint(7,value->height()/2));
        QCOMPARE(value->value(),0); QCOMPARE(overlay->annotations().first().color.name(),QStringLiteral("#000000"));
        auto *preview=overlay->findChild<QToolButton *>("colorPreview"); QVERIFY(preview);
        QCOMPARE(preview->text(),QStringLiteral("#000000"));
    }
    void everyStyleSliderHasVisibleReachableEndpoints() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        for (const auto &entry : {qMakePair("tool_3","widthControl"),qMakePair("tool_5","fontControl"),qMakePair("tool_5","fontWeightControl")}) {
            if (!overlay->findChild<QToolButton *>(entry.first)->isChecked()) QTest::mouseClick(overlay->findChild<QToolButton *>(entry.first),Qt::LeftButton);
            auto *slider=overlay->findChild<QSlider *>(entry.second); QVERIFY(slider); QVERIFY(slider->isVisible());
            for (const int target : {slider->minimum(),slider->maximum()}) {
                slider->setValue(target);
                const int x=sliderHandleCenterX(slider);
                QVERIFY(x-7>=0); QVERIFY(x+7<slider->width());
                QTest::mousePress(slider,Qt::LeftButton,Qt::NoModifier,QPoint(x,slider->height()/2));
                QTest::mouseRelease(slider,Qt::LeftButton,Qt::NoModifier,QPoint(x,slider->height()/2));
                QCOMPARE(slider->value(),target);
            }
        }
    }
    void textEscapeCommitsDraftAndNextEscapeCancelsCapture() {
        int writes=0;
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[&](const QImage &,QString *) { ++writes; return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished);
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("保留这段文字")); QApplication::sendEvent(editor,&input);
        QVERIFY(overlay->annotations().isEmpty());
        QTest::keyClick(editor,Qt::Key_Escape);
        QCOMPARE(finished.count(),0); QCOMPARE(overlay->annotations().size(),1);
        QCOMPARE(overlay->annotations().first().text,QStringLiteral("保留这段文字"));
        QVERIFY(!overlay->findChild<QFrame *>("stylePanel")->isVisible());
        QTest::keyClick(overlay,Qt::Key_Escape);
        QCOMPARE(finished.count(),1); QCOMPARE(writes,0);
    }
    void textDraftUndoIsSeparateAndUnchangedCommitIsNoOp() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        selectArea(overlay,QPoint(50,180),QPoint(300,200));
        const int depth=overlay->undoDepth();
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("Draft")); QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Z,Qt::ControlModifier);
        QVERIFY(editor->toPlainText().isEmpty()); QCOMPARE(overlay->undoDepth(),depth); QCOMPARE(overlay->annotations().size(),1);
        QTest::keyClick(editor,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);
        QCOMPARE(editor->toPlainText(),QStringLiteral("Draft"));
        QTest::keyClick(editor,Qt::Key_Escape);
        QCOMPARE(overlay->undoDepth(),depth+1); QCOMPARE(overlay->annotations().size(),2);
        const Annotation original=overlay->annotations().last();
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_0"),Qt::LeftButton);
        QTest::mouseDClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(55,60));
        auto *draft=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(draft);
        QCOMPARE(overlay->annotations().last().text,original.text);
        QTest::keyClick(draft,Qt::Key_Escape);
        QCOMPARE(overlay->undoDepth(),depth+1); QCOMPARE(overlay->annotations().last().id,original.id);
        QTest::keyClick(overlay,Qt::Key_Z,Qt::ControlModifier);
        QCOMPARE(overlay->annotations().size(),1);
    }
    void preeditEscapeDoesNotCloseTextOrCapture() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished);
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent preedit(QStringLiteral("ni"),{}); QApplication::sendEvent(editor,&preedit);
        QTest::keyClick(editor,Qt::Key_Escape);
        QVERIFY(editor->isVisible()); QCOMPARE(finished.count(),0); QVERIFY(overlay->annotations().isEmpty());
        QInputMethodEvent committed; committed.setCommitString(QStringLiteral("你好")); QApplication::sendEvent(editor,&committed);
        QTest::keyClick(editor,Qt::Key_Return);
        QVERIFY(editor->toPlainText().contains(QLatin1Char('\n')));
        QTest::keyClick(editor,Qt::Key_Escape);
        QCOMPARE(overlay->annotations().first().text,QStringLiteral("你好\n"));
    }
    void overflowingTextKeepsAnchorAndFullContents() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(250,160));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        overlay->findChild<QSlider *>("fontControl")->setValue(34);
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,130));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("第一行\n第二行\n第三行\n第四行")); QApplication::sendEvent(editor,&input);
        QCOMPARE(editor->pos(),QPoint(40,130));
        auto *hint=overlay->findChild<QLabel *>("textOverflowHint"); QVERIFY(hint); QVERIFY(hint->isVisible());
        const QString full=editor->toPlainText(); QTest::keyClick(editor,Qt::Key_Escape);
        QCOMPARE(overlay->annotations().first().text,full);
        QCOMPARE(overlay->annotations().first().rect.topLeft(),QPointF(40,130));
        QVERIFY(overlay->annotations().first().rect.bottom()>160);
    }
    void toolShortcutsConstrainDrawingAndNudgeSelectedObject() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::keyClick(overlay,Qt::Key_A);
        QVERIFY(overlay->findChild<QToolButton *>("tool_3")->isChecked());
        QTest::mousePress(overlay,Qt::LeftButton,Qt::ShiftModifier,QPoint(50,100));
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::ShiftModifier,QPoint(310,180));
        QCOMPARE(overlay->annotations().size(),1);
        const AnnotationId arrow=overlay->annotations().first().id;
        const QRectF before=overlay->annotations().first().rect;
        QVERIFY(qAbs(before.height())<0.001);
        QTest::keyClick(overlay,Qt::Key_V);
        QTest::keyClick(overlay,Qt::Key_Right);
        QTest::keyClick(overlay,Qt::Key_Down,Qt::ShiftModifier);
        QCOMPARE(overlay->annotations().first().id,arrow);
        QCOMPARE(overlay->annotations().first().rect.topLeft(),before.topLeft()+QPointF(1,10));
        QTest::keyClick(overlay,Qt::Key_R);
        QTest::mousePress(overlay,Qt::LeftButton,Qt::ShiftModifier,QPoint(100,150));
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::ShiftModifier,QPoint(240,210));
        const QRectF square=overlay->annotations().last().rect.normalized();
        QCOMPARE(square.width(),square.height());
        QVERIFY(overlay->findChild<QToolButton *>("tool_1")->isChecked());
    }
    void escapeDuringDragRestoresSelectionAndLeavesHistoryUntouched() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished);
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::keyClick(overlay,Qt::Key_R); selectArea(overlay,QPoint(100,100),QPoint(250,180));
        QTest::keyClick(overlay,Qt::Key_V);
        const QRectF before=overlay->annotations().first().rect; const int depth=overlay->undoDepth();
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(100,140));
        QTest::mouseMove(overlay,QPoint(60,140));
        QTest::keyClick(overlay,Qt::Key_Escape);
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(60,140));
        QCOMPARE(overlay->annotations().first().rect,before); QCOMPARE(overlay->undoDepth(),depth); QCOMPARE(finished.count(),0);
        // 离屏插件不会稳定地把 QCursor 的原生移动转成 QWidget hover，显式投递同坐标事件。
        QTest::mouseMove(overlay,QPoint(10,10));
        QMouseEvent hover(QEvent::MouseMove,QPointF(10,10),QPointF(overlay->mapToGlobal(QPoint(10,10))),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(overlay,&hover);
        QCOMPARE(overlay->cursor().shape(),Qt::SizeFDiagCursor);
    }
    void rectangleSideAndTextWidthHandlesWork() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(510,400));
        QTest::keyClick(overlay,Qt::Key_R); selectArea(overlay,QPoint(100,100),QPoint(250,180)); QTest::keyClick(overlay,Qt::Key_V);
        selectArea(overlay,QPoint(100,140),QPoint(60,140));
        QCOMPARE(overlay->annotations().first().rect.normalized(),QRectF(60,100,190,80));
        QTest::keyClick(overlay,Qt::Key_T); QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,230));
        auto *editor=overlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QInputMethodEvent input; input.setCommitString(QStringLiteral("Text wraps at its layout width and keeps the same font size.")); QApplication::sendEvent(editor,&input);
        QTest::keyClick(editor,Qt::Key_Escape); QTest::keyClick(overlay,Qt::Key_V);
        const Annotation original=overlay->annotations().last();
        const QPoint handle=annotationHandles(original).last().toPoint();
        selectArea(overlay,handle,handle+QPoint(120,0));
        QCOMPARE(overlay->annotations().last().fontSize,original.fontSize);
        QVERIFY(overlay->annotations().last().rect.width()>original.rect.width());
    }
    void keyboardNudgeMovesCropAndTextLettersStayInEditor() {
        ScreenFrame f=frame(); f.screens[0].image.setPixelColor(22,40,Qt::blue);
        QImage copied;
        auto *overlay=new CaptureOverlay(f,QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(210,110));
        QTest::keyClick(overlay,Qt::Key_Right); QTest::keyClick(overlay,Qt::Key_Down,Qt::ShiftModifier);
        QTest::keyClick(overlay,Qt::Key_Return);
        QCOMPARE(copied.size(),QSize(400,200)); QCOMPARE(copied.pixelColor(0,0),QColor(Qt::blue));
        auto *textOverlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        textOverlay->show(); selectArea(textOverlay,QPoint(10,10),QPoint(510,400)); QTest::keyClick(textOverlay,Qt::Key_T);
        QTest::mouseClick(textOverlay,Qt::LeftButton,Qt::NoModifier,QPoint(40,50));
        auto *editor=textOverlay->findChild<QTextEdit *>("annotationTextEditor"); QVERIFY(editor);
        QTest::keyClicks(editor,QStringLiteral("vroaptm"));
        QCOMPARE(editor->toPlainText(),QStringLiteral("vroaptm")); QVERIFY(textOverlay->findChild<QToolButton *>("tool_5")->isChecked());
    }
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
        done->click(); QCOMPARE(attempts,1); QCOMPARE(finished.count(),0);
        auto *error=overlay->findChild<QFrame *>("copyErrorBanner"); QVERIFY(error); QVERIFY(error->isVisible());
        QVERIFY(overlay->isVisible()); QCOMPARE(overlay->annotations().size(),1);
        auto *retry=overlay->findChild<QToolButton *>("retryButton"); QVERIFY(retry);
        QTest::mouseClick(retry,Qt::LeftButton);
        QCOMPARE(attempts,2); QCOMPARE(finished.count(),1); QVERIFY(!copied.isNull());
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
    void sliderHandleMatchesPointerPosition() {
        // 自绘滑块的手柄位置必须与命中位置一致：按在看到的手柄上，值不该跳走。
        // 之前手柄位置算错（sliderPositionFromValue 的 span/offset 传反），
        // 视觉手柄与真实位置完全脱节，用户表现为「拖不动、逻辑错乱」。
        // 原有用例都只 setValue()，从未走过真实鼠标命中路径，所以没能挡住这个回归。
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        QCoreApplication::processEvents();
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        QVERIFY(width->isVisible());
        width->setValue(7);
        const int handle=sliderHandleCenterX(width);
        QVERIFY(handle>0);
        QVERIFY(handle<width->width());
        // 按在看到的手柄上：值保持不变，而不是跳到最大或最小。
        QTest::mousePress(width,Qt::LeftButton,Qt::NoModifier,QPoint(handle,width->height()/2));
        QTest::mouseRelease(width,Qt::LeftButton,Qt::NoModifier,QPoint(handle,width->height()/2));
        QCOMPARE(width->value(),7);
        // 从手柄位置往轨道左端拖，值应当跟着变小。
        QTest::mousePress(width,Qt::LeftButton,Qt::NoModifier,QPoint(handle,width->height()/2));
        QTest::mouseMove(width,QPoint(2,width->height()/2));
        QTest::mouseRelease(width,Qt::LeftButton,Qt::NoModifier,QPoint(2,width->height()/2));
        QVERIFY(width->value()<7);
        // 必须真正销毁覆盖层：样式面板打开时 Escape 只关面板，
        // 覆盖层会继续存活，其去抖定时器会在后续用例里写设置、污染别的用例。
        overlay->close(); QCoreApplication::processEvents();
    }
    void sliderSettingsArePersistedAfterDebounce() {
        // 滑块停止变化后要落盘：拖动过程中每次变化都写盘是同步 IO，会让拖动发涩。
        // 用对象自己的观察接口断言，不依赖全局 QSettings 状态，避免用例之间互相污染。
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();
        auto *width=overlay->findChild<QSlider *>("widthControl"); QVERIFY(width);
        QCOMPARE(overlay->persistedLineWidth(),0);      // 还没落盘过
        const int target=width->value()==8?7:8;         // 必须与当前值不同，才会触发 valueChanged
        width->setValue(target);
        QCOMPARE(overlay->persistedLineWidth(),0);      // 刚改完，还没到去抖间隔，不应落盘
        QTest::qWait(600);                              // 等去抖间隔过去
        QCOMPARE(overlay->persistedLineWidth(),target);  // 此时应已落盘
        overlay->close(); QCoreApplication::processEvents();
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
    void rightClickReselectKeepsAnnotations() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();      // 箭头
        selectArea(overlay,QPoint(50,100),QPoint(350,100));        // 画一个箭头
        QCOMPARE(overlay->annotations().size(),1);
        const int steps=overlay->undoDepth();
        // 右键重新框选：选区应清空，但标注与撤销历史必须保留。
        QTest::mouseClick(overlay,Qt::RightButton,Qt::NoModifier,QPoint(200,200));
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().size(),1);
        QCOMPARE(overlay->undoDepth(),steps);
        auto *undo=overlay->findChild<QToolButton *>("undoButton"); QVERIFY(undo);
        QVERIFY(undo->isEnabled());
        // 撤销仍应能逐个移除那个箭头。
        undo->click();
        QCOMPARE(overlay->annotations().size(),0);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void clickOutsideSelectionKeepsAnnotations() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(300,220));
        overlay->findChild<QToolButton *>("tool_3")->click();
        selectArea(overlay,QPoint(50,100),QPoint(250,180));
        QCOMPARE(overlay->annotations().size(),1);
        overlay->findChild<QToolButton *>("tool_0")->click();      // 选择工具
        const int steps=overlay->undoDepth();
        // 在选区外按下并拖动，是重新框选的另一条路径，同样不能丢标注。
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(330,300));
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(400,340));
        QCoreApplication::processEvents();
        QCOMPARE(overlay->annotations().size(),1);
        QCOMPARE(overlay->undoDepth(),steps);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void stylePanelTogglesWithToolbarButton() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *panel=overlay->findChild<QFrame *>("stylePanel"); QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        auto *width=overlay->findChild<QToolButton *>("tool_3"); QVERIFY(width);
        width->click();
        QVERIFY(panel->isVisible());
        // 再点一次收起。
        width->click();
        QVERIFY(!panel->isVisible());
        // 面板内的滑块 objectName 保持不变，既有断言继续有效。
        QVERIFY(overlay->findChild<QSlider *>("widthControl"));
        QVERIFY(overlay->findChild<QSlider *>("fontControl"));
        QVERIFY(overlay->findChild<QSlider *>("fontWeightControl"));
        QVERIFY(overlay->findChild<QLabel *>("widthValue"));
        QVERIFY(overlay->findChild<QLabel *>("fontValue"));
        QVERIFY(overlay->findChild<QLabel *>("fontWeightValue"));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void stylePanelIsInsideToolbarWithoutCoveringActions() {
        auto *overlay=new CaptureOverlay(frame(320,260),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(10,10),QPoint(300,220));
        auto *panel=overlay->findChild<QFrame *>("stylePanel"); QVERIFY(panel);
        auto *toolbar=overlay->findChild<QFrame *>("captureToolbar"); QVERIFY(toolbar);
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_5"),Qt::LeftButton);
        QCoreApplication::processEvents();
        QVERIFY(panel->isVisible()); QVERIFY(overlay->rect().contains(toolbar->geometry()));
        const QRect panelRect(panel->mapTo(overlay,QPoint()),panel->size());
        QVERIFY(toolbar->geometry().contains(panelRect));
        for (const char *name : {"doneButton","cancelButton"}) {
            auto *button=overlay->findChild<QToolButton *>(name); QVERIFY(button); QVERIFY(button->isVisible());
            const QRect buttonRect(button->mapTo(overlay,QPoint()),button->size());
            QVERIFY(!buttonRect.intersects(panelRect)); QVERIFY(overlay->rect().contains(buttonRect));
        }
        QVERIFY(!overlay->findChild<QSlider *>("widthControl")->isVisible());
        QVERIFY(overlay->findChild<QSlider *>("fontControl")->isVisible());
        QVERIFY(overlay->findChild<QSlider *>("fontWeightControl")->isVisible());
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_6"),Qt::LeftButton);
        QVERIFY(!panel->isVisible()); QVERIFY(!overlay->findChild<QToolButton *>("colorButton")->isEnabled());
    }
    void toolbarAndColorsStayOnAnActualScreen() {
        ScreenFrame f=dualFrame();
        f.screens[1].geometry=QRect(600,200,600,480); f.virtualGeometry=QRect(0,0,1200,680);
        auto *overlay=new CaptureOverlay(f,QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); selectArea(overlay,QPoint(550,250),QPoint(760,340));
        QTest::mouseClick(overlay->findChild<QToolButton *>("tool_3"),Qt::LeftButton);
        QCoreApplication::processEvents();
        auto *toolbar=overlay->findChild<QFrame *>("captureToolbar"); QVERIFY(toolbar);
        const QRect screen(600,200,600,480);
        QVERIFY(screen.contains(toolbar->geometry()));
        QTest::mouseClick(overlay->findChild<QToolButton *>("colorButton"),Qt::LeftButton);
        auto *colors=overlay->findChild<QFrame *>("colorPanel"); QVERIFY(colors); QVERIFY(colors->isVisible());
        QVERIFY(screen.contains(colors->geometry())); QVERIFY(!colors->geometry().intersects(toolbar->geometry()));
    }
    void stylePanelEscOnlyClosesPanel() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        QSignalSpy finished(overlay,&CaptureOverlay::finished);
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *panel=overlay->findChild<QFrame *>("stylePanel"); QVERIFY(panel);
        overlay->findChild<QToolButton *>("tool_3")->click();
        QVERIFY(panel->isVisible());
        // 面板打开时 Esc 只收起面板，截图继续。
        QTest::keyClick(overlay,Qt::Key_Escape);
        QVERIFY(!panel->isVisible());
        QCOMPARE(finished.count(),0);
        QVERIFY(overlay->isVisible());
        // 再按一次 Esc 才取消截图。
        QTest::keyClick(overlay,Qt::Key_Escape);
        QCOMPARE(finished.count(),1);
    }
    void toolButtonsCarryChineseLabels() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        const QStringList expected={QStringLiteral("选择"),QStringLiteral("矩形"),QStringLiteral("椭圆"),QStringLiteral("箭头"),
                                    QStringLiteral("画笔"),QStringLiteral("文字"),QStringLiteral("马赛克")};
        for (int i=0;i<expected.size();++i) {
            auto *button=overlay->findChild<QToolButton *>(QStringLiteral("tool_%1").arg(i));
            QVERIFY(button);
            QCOMPARE(button->text(),expected.at(i));
            QCOMPARE(button->accessibleName(),expected.at(i));
            QVERIFY(button->toolTip().contains(expected.at(i)));
            QCOMPARE(button->size(),QSize(32,32));
            QVERIFY(button->toolButtonStyle()==Qt::ToolButtonIconOnly);
            QVERIFY(!button->icon().isNull());
        }
        auto *undo=overlay->findChild<QToolButton *>("undoButton"); QVERIFY(undo);
        QCOMPARE(undo->text(),QStringLiteral("撤销"));
        auto *redo=overlay->findChild<QToolButton *>("redoButton"); QVERIFY(redo);
        QCOMPARE(redo->text(),QStringLiteral("重做"));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void colorPanelNeverCoversToolbar() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *panel=overlay->findChild<QFrame *>("colorPanel"); QVERIFY(panel);
        auto *toolbar=overlay->findChild<QFrame *>("captureToolbar"); QVERIFY(toolbar);
        overlay->findChild<QToolButton *>("colorButton")->click();
        QVERIFY(panel->isVisible());
        QVERIFY2(!panel->geometry().intersects(toolbar->geometry()),
                 qPrintable(QStringLiteral("取色面板 %1,%2 %3×%4 工具栏 %5,%6 %7×%8")
                            .arg(panel->geometry().x()).arg(panel->geometry().y())
                            .arg(panel->width()).arg(panel->height())
                            .arg(toolbar->geometry().x()).arg(toolbar->geometry().y())
                            .arg(toolbar->width()).arg(toolbar->height())));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void colorPanelAppliesPresetColor() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        auto *panel=overlay->findChild<QFrame *>("colorPanel"); QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        auto *colorButton=overlay->findChild<QToolButton *>("colorButton"); QVERIFY(colorButton);
        colorButton->click();
        QVERIFY(panel->isVisible());
        overlay->applyChosenColor(QColor("#123456"));
        const quintptr before=colorButton->icon().cacheKey();
        auto *preset=overlay->findChild<QToolButton *>("preset_1"); QVERIFY(preset);   // #ff6b6b
        preset->click();
        // 预设色写入设置：颜色以十六进制字符串存放，跨后端行为一致。
        QTRY_COMPARE_WITH_TIMEOUT(QSettings().value("annotation/tools/rectangle/color").toString(),QStringLiteral("#ff6b6b"),3000);
        // 工具栏色块按钮的图标随之更新
        QVERIFY(colorButton->icon().cacheKey()!=before);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void colorPanelUpdatesSelectedAnnotation() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();      // 箭头
        selectArea(overlay,QPoint(50,100),QPoint(350,100));
        overlay->findChild<QToolButton *>("tool_0")->click();      // 选择工具
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));
        QCOMPARE(overlay->annotations().size(),1);
        overlay->findChild<QToolButton *>("colorButton")->click();
        auto *preset=overlay->findChild<QToolButton *>("preset_4"); QVERIFY(preset);   // #ffb340
        preset->click();
        // 选中标注的颜色同步更新
        QCOMPARE(overlay->annotations().first().color.name(),QStringLiteral("#ffb340"));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void recentColorsAreRemembered() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->applyChosenColor(QColor("#00b050"));
        // 最近使用的颜色以逗号分隔的字符串存放，读回时不再依赖后端保留列表类型。
        const QVariant recentValue=QSettings().value("annotation/recentColors");
        QStringList stored=recentValue.toString().split(',',Qt::SkipEmptyParts);
        if (stored.isEmpty()) stored=recentValue.toStringList();
        for (QString &name : stored) name=name.trimmed();
        QVERIFY(!stored.isEmpty());
        QCOMPARE(stored.first(),QStringLiteral("#00b050"));
        auto *recent=overlay->findChild<QToolButton *>("recent_0"); QVERIFY(recent);
        QVERIFY(recent->isEnabled());
        // 点最近使用的第一个色块能取回刚才的颜色。
        recent->click();
        QTRY_COMPARE_WITH_TIMEOUT(QSettings().value("annotation/tools/rectangle/color").toString(),QStringLiteral("#00b050"),3000);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void overlayCoversWholeVirtualDesktop() {
        auto *overlay=new CaptureOverlay(dualFrame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        // 覆盖层必须铺满整个虚拟桌面，选区才能横跨两块屏。
        QCOMPARE(overlay->geometry(),QRect(0,0,1200,480));
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
    void crossScreenSelectionSpansBothScreens() {
        QImage copied;
        auto *overlay=new CaptureOverlay(dualFrame(),QGuiApplication::primaryScreen(),[&](const QImage &image,QString *) { copied=image; return true; });
        overlay->show(); QCoreApplication::processEvents();
        // 选区横跨左右两屏：左屏 2x、右屏 1x。
        selectArea(overlay,QPoint(500,100),QPoint(700,300));
        QTest::keyClick(overlay,Qt::Key_Return);
        QVERIFY(!copied.isNull());
        // 输出比例取两屏中的最大值 2x，选区逻辑尺寸 200×200。
        QCOMPARE(copied.size(),QSize(400,400));
    }
    void draggingAnnotationDoesNotRecropBase() {
        auto *overlay=new CaptureOverlay(frame(),QGuiApplication::primaryScreen(),[](const QImage &,QString *) { return true; });
        overlay->show(); QCoreApplication::processEvents();
        selectArea(overlay,QPoint(10,10),QPoint(510,400));
        overlay->findChild<QToolButton *>("tool_3")->click();      // 箭头
        selectArea(overlay,QPoint(50,100),QPoint(350,100));        // 画一个箭头
        overlay->findChild<QToolButton *>("tool_0")->click();      // 选择工具
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));  // 选中箭头
        overlay->repaint();
        const int crops=overlay->cropCount();
        QVERIFY(crops>0);
        // 拖动标注时选区不变，底图不应每帧重新裁剪。
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,100));
        for (int i=1;i<=10;++i) { QTest::mouseMove(overlay,QPoint(200+i,100)); overlay->repaint(); }
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(210,100));
        overlay->repaint();
        QCOMPARE(overlay->cropCount(),crops);
        QTest::keyClick(overlay,Qt::Key_Escape);
    }
private:
    static int undoSteps(CaptureOverlay *overlay) { return overlay->undoDepth(); }
};
QTEST_MAIN(OverlayTests)
#include "overlay_tests.moc"
