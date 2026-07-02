#include "widget.h"
#include "ui_widget.h"
#include "fileloader.h"
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QtEndian>
#include "fftstuff.h"
#include <qtimer.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <QMessageBox>
#include <QUrl>
#include <QTimer>

extern bool collectMicData;
extern double rec_arr[];    // DEFINED AS DOUBLE FOR FFTW
extern int rec_arr_cnt;
extern int frame_start;
extern int frame_size;
extern int frame_end;
extern int rec_arr_end;
extern QString currentlesson;
//extern QString gNote;
const QList <QString> note_letters = {"C", "C#", "D", "D#", "E",
                                     "F", "F#", "G", "G#", "A", "A#", "B" };
int curLessonInt;
//int shiftedIndex = 0;
int orientation [21] = {1,2,3,4,5,6,7,8,1,8,1,8,1,8,7,6,5,4,3,2,1};
QMap<QString, int> tonic_map = {
    {"G3", 43}, {"A3", 45}, {"B3", 47}, {"C4", 48}, {"D4", 50}, {"A4", 52}, {"B4", 54}, {"C5", 55}
};
QMap<int, QString> tilesGoodBadMap = {
    {0, "note0"}, {1, "note2"}, {2, "note4"}, {3, "note5"}, {4, "note7"}, {5, "note9"},
    {6, "note11"}, {7, "note12"}
};
QMap<int, int> tileKbShift = {
    {0, 0}, {1, 2}, {2, 4}, {3, 5}, {4, 7}, {5, 9}, {6, 11}, {7, 12}
};

QList<int> kbNotePlayLists;
extern QList<QByteArray> rawRecArrays;
FftStuff ftw;
int accValue = 0;
int displayDuration = 3000;
int playDuration = 3000;


Microphone::Microphone(const QAudioFormat &format) : m_format(format) {
    qDebug()<<" YOU SHOULD SEE THIS ";
}

Microphone::~Microphone()
{

}

void Microphone::start()
{
    open(QIODevice::WriteOnly);
}

void Microphone::stop()
{
    close();
}

qint64 Microphone::readData(char * /* data */, qint64 /* maxlen */)      // NOT USED IN EXAMPLE
{
    return 0;
}

qreal Microphone::getNoteValue(const char *data, qint64 len) const
{
    const int channelBytes = m_format.bytesPerSample();
    const int sampleBytes = m_format.bytesPerFrame();
    const int numSamples = len / sampleBytes;

    float maxValue = 0;
    auto *ptr = reinterpret_cast<const unsigned char *>(data);

    for (int i = 0; i < numSamples; ++i) {
        float value = m_format.normalizedSampleValue(ptr);
        rec_arr[rec_arr_cnt]=value;
        //maxValue = qMax(value, maxValue);
        ptr += channelBytes;
        rec_arr_cnt++;
    };
    if(rec_arr_cnt > frame_end){
        cout<<"\n  NEXT FRAME $$$$ "<<frame_start<<endl;

        ftw.DoIt(frame_start, frame_size);
        frame_start = frame_end;
        frame_end = frame_end + frame_size;}
    if (rec_arr_cnt > 200000)
    {
        qDebug() <<"                      restart here";
        rec_arr_cnt = 0;

        emit on_TimeOut();
        qDebug() <<"                      post restart here";
        return 0;
    }
    return maxValue;
}

qint64 Microphone::writeData(const char *data, qint64 len)
{
    // qDebug() << "enter writeData" << rec_arr_cnt;
    m_level = getNoteValue(data, len);
    return len;
}

Speaker::Speaker()
{

}

void Speaker::start()
{
    open(QIODevice::ReadOnly);
}

void Speaker::stop()
{
    m_pos = 0;
    close();
}

void Speaker::newTest(QByteArray bufferOut)
{
    qDebug()<<"  %%%%  NEW TEST ";
    m_buffer.assign(bufferOut);
    m_buffer.clear();
    qDebug()<<&m_buffer<<" m_buffer_.size() " << m_buffer.size();
    m_buffer = bufferOut;
    qDebug()<<&m_buffer<<" m_buffer_.size() " << m_buffer.size();
}

void Speaker::clearBuffer()
{
    QByteArray emptyArray = {};
    m_buffer.assign(emptyArray);
    m_buffer.clear();
    m_buffer = emptyArray;
}

qint64 Speaker::readData(char *data, qint64 len)
{
    qDebug() << "enter speaker readdata...is on main? " << QThread::isMainThread();
    qint64 total = 0;
    if (!m_buffer.isEmpty()) {
        while (len - total > 0) {
            const qint64 chunk = qMin((m_buffer.size() - m_pos), len - total);
            memcpy(data + total, m_buffer.constData() + m_pos, chunk);
            m_pos = (m_pos + chunk) % m_buffer.size();
            total += chunk;
            qDebug() << "chunk..." << chunk << "pos> = " << m_pos ;
        }
    }
    return total;
}

qint64 Speaker::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

qint64 Speaker::bytesAvailable() const
{
    return QIODevice::bytesAvailable();
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);    
    this->setWindowTitle("Full Quiz tester");
    initializeWindow();
    initializeAudioInput(QMediaDevices::defaultAudioInput());
    initializeAudioOutput(m_devicesOut->defaultAudioOutput());
    QPixmap pix(":/img/down-arrow.png");
    ui->lb_arrow->setPixmap(pix);
    ui->lb_arrow->move(800, 100);
    FileLoader::ReadConfig();
    FileLoader::ReadLesson();
    ui->lb_review->setText("Start");
    ui->lb_title->setText("Lesson");
    accValue = 0;
    qDebug() << "currentlesson = " << currentlesson;
    curLessonInt = currentlesson.toInt() - 1;
    FileLoader::GetRandomTestSet(gTestGroup[curLessonInt]);
    FftStuff fts;
    orientationFlag = true;
    m_timer = new QTimer(this);
    m_Microphone->moveToThread(&MicThread);
    MicThread.setObjectName("MicThread");
    MicThread.start();
    m_Speaker->moveToThread(&SpeakerThread);
    connect(&ftw, &FftStuff::valueChanged,this, &Widget::updateKBnote, Qt::QueuedConnection);
    connect(&ftw, &FftStuff::on_foundNote,this, &Widget::Got_Note, Qt::QueuedConnection);
    connect(m_Microphone, &Microphone::on_TimeOut, this, &Widget::TimeOut);
    connect(m_timer,&QTimer::timeout,this,&Widget::TimeOut);
}

Widget::~Widget()
{
    MicThread.terminate();
    SpeakerThread.terminate();
    delete m_audioSource;
    delete m_Microphone;
    delete ui;
}

void Widget::initializeWindow()
{
    const QAudioDevice &defaultDeviceInfo = QMediaDevices::defaultAudioInput();
}

void Widget::initializeAudioInput(const QAudioDevice &deviceInfo)
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_Microphone = (new Microphone(format));
    m_audioSource = (new QAudioSource(deviceInfo, format));
    qDebug() <<  "buffer size: " << m_audioSource->bufferSize();
    m_Microphone->start();
}

void Widget::initializeAudioOutput(const QAudioDevice &deviceInfo)
{
    qDebug() << "initializeAudioOutput...";
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    qDebug() << "     !!!   from  INIT format: " << format.sampleRate();
    m_Speaker.reset(new Speaker());
    m_audioOutput.reset(new QAudioSink(deviceInfo, format));
}

void buildkbNotePlayList(int tonicNote)
{
    //QList<int> kbNotePlayLists;
    int kbNoteFileName = tonicNote;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 2;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 4;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 5;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 7;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 9;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 11;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 12;
    kbNotePlayLists.append(kbNoteFileName);


    qDebug() << kbNotePlayLists;
}

void Widget::paintEvent(QPaintEvent * /* event */)
{
    QPixmap pix(100,60);
    pix.fill(Qt::white);
    //create a QPainter and pass a pointer to the device.
    QPainter *painter = new QPainter(&pix);
    QPen outsidePen(Qt::red, 4, Qt::SolidLine);
    painter->setPen(outsidePen);
    painter->drawEllipse(35, 15, 30, 30);
    QPen insidePen(Qt::green, 4, Qt::SolidLine);
    painter->setPen(insidePen);
    painter->drawEllipse(40 + accValue, 20, 20, 20);
    QPen linePen(Qt::black, 1, Qt::SolidLine);
    painter->setPen(linePen);
    painter->drawLine(0, 30, 100, 30);
    painter->drawLine(50, 0, 50, 60);
    painter->end();
    ui->lb_tuner->setPixmap(pix);
}

void Widget::on_btnStart_clicked()
{    
    restartAudioStream();
    qDebug() << "start pushed...";
    ui->btnStart->setVisible(false);
    ui->btnTunerON->setVisible(false);
    qDebug() << "starting...";
    QString temp = "Lesson #" + currentlesson + " Key " + gKey[curLessonInt]
                   + " Test Notes " + gTestGroup[curLessonInt];
    ui->lb_title->setText(temp);
    ui->lb_score->setText("0 of 0");
    ui->lb_info->setText("up scale 1 to 7\n octaves 3 times\ndown scale 7 to 1");
    curLessonInt = currentlesson.toInt();

    // get sound array set
    tonicNote = tonic_map[gNote[curLessonInt]];
    qDebug() << tonicNote;
    qDebug() << gNote[curLessonInt];
    FileLoader files;
    files.GetFileList(tonicNote);
    buildkbNotePlayList(tonicNote);
    playedCnt = 0;
    goodCnt = 0;
    nPos = 0;
    do_Orientation(nPos);
    nPos++;
}

void Widget::restartAudioStream()
{
    m_audioSource->stop();
    qDebug()<< "is main Thread: " << QThread::isMainThread();
    m_audioSource->start(m_Microphone);
    qDebug() << "============================ restartAudioStream====>>>";
}

void Widget::do_Orientation(int nPos)
{
    m_timer->setInterval(playDuration);
    m_timer->start();
    playedNote = orientation[nPos] - 1;
    kbPlayed = kbNotePlayLists[orientation[nPos] - 1];
    m_Speaker->newTest( rawRecArrays[orientation[nPos] - 1]);
    qDebug() << "orientation value: " << orientation[nPos] - 1;
    m_Speaker->stop();
    m_Speaker->start();
    m_audioOutput->stop();
    m_audioOutput->start(m_Speaker.data());
    SpeakerThread.start();
}

void Widget::do_Quiz(int nPos)
{
    m_timer->setInterval(playDuration);
    m_timer->start();
    playedNote = testNotes[nPos] - 1;
    kbPlayed = kbNotePlayLists[testNotes[nPos] - 1];
    m_Speaker->newTest( rawRecArrays[testNotes[nPos] - 1]);
    qDebug() << "testNotes value: " << testNotes[nPos] - 1;
    m_Speaker->stop();
    m_Speaker->start();
    m_audioOutput->stop();
    m_audioOutput->start(m_Speaker.data());
    SpeakerThread.start();
}

void Widget::updateKBnote(int kbValue, float acc)
{
    int letter = kbValue%12;
    int octaveValue = kbValue/12;
    QString theNote = note_letters[letter] + QString::number(octaveValue);
    qDebug() << "knNote... " << theNote;
    QString alphaNote = note_letters[letter] + QString::number(octaveValue);
    qDebug() << "alphaNote... " << alphaNote;
    ui->lb_note->setText(alphaNote);
    qDebug() << "acc is: " << (int)(acc * 1000);
    accValue = (int)(acc * 1000);
    ui->lb_tuner->repaint();
}

void Widget::play_next_note()
{
    qDebug() << "next pressed";
    qDebug() << "SpeakerThread : " << SpeakerThread.isRunning();
    qDebug() << "micThread running: " << MicThread.isRunning();
    qDebug() << "m_Microphone is open: " << m_Microphone->isOpen();
    // zero out rec_arr with each mic get
    for(int i = 0; i < 200000; i++)
    {
        rec_arr[i] = 0;
    }
    rec_arr_cnt = 0;
    frame_start = 0;
    frame_end = 2048;
    m_Microphone->reset();
    m_audioSource->reset();
    restartAudioStream();
    QThread::msleep(100);
    if (orientationFlag)
    {
        do_Orientation(nPos);
    }
    else
    {
        do_Quiz(nPos);
    }

    nPos++;
}

void Widget::TimeOut()
{
    m_timer->stop();
    m_audioSource->suspend();
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Timed Out", "Continue?",
                                  QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        qDebug() << "continuing.";
        timeoutRetry();
    } else {
        qDebug() << "No was clicked";
        QApplication::quit();
        qDebug() << "app quit called...";
    }
}

void Widget::timeoutRetry()
{
    qDebug() << "continuing...>>>";
    m_audioSource->resume();
    for(int i = 0; i < 200000; i++)
    {
        rec_arr[i] = 0;
    }
    rec_arr_cnt = 0;
    frame_start = 0;
    frame_end = 2048;
    m_Microphone->reset();
    m_audioSource->reset();
    restartAudioStream();
    QThread::msleep(100);
    nPos--;
    if (orientationFlag)
    {
        do_Orientation(nPos);
    }
    else
    {
        do_Quiz(nPos);
    }
    nPos++;
}

void Widget::Got_Note(int kbValue)
{
    playedCnt++;
    m_audioSource->stop();
    ui->progBarPlayed->setValue(playedCnt);
    if (kbValue == kbPlayed)
    {
        goodCnt++;
        ui->progBarGood->setValue(goodCnt);
    }
    QString temp = QString::number(goodCnt) + " of " + QString::number(playedCnt);
    ui->lb_score->setText(temp);
    qDebug() << "-->note found: " << kbValue;
    // stopSound();
    int heardNote = kbValue;
    ui->lb_arrow->move(55+((heardNote - tonicNote)*45), 115);
    ui->lb_arrow->repaint();
    kbPlayedNote = tonicNote + tileKbShift[playedNote];
    qDebug() << "-->heardNote: " << heardNote << " = " << kbPlayedNote;
    switch (playedNote){
    case 0:
        if (kbPlayedNote == heardNote)
        {
            ui->note0->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note0->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 1:
        if (kbPlayedNote == heardNote)
        {
            ui->note1->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note1->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 2:
        if (kbPlayedNote == heardNote)
        {
            ui->note2->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note2->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 3:
        if (kbPlayedNote == heardNote)
        {
            ui->note3->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note3->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 4:
        if (kbPlayedNote == heardNote)
        {
            ui->note4->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note4->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 5:
        if (kbPlayedNote == heardNote)
        {
            ui->note5->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note5->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 6:
        if (kbPlayedNote == heardNote)
        {
            ui->note6->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note6->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 7:
        if (kbPlayedNote == heardNote)
        {
            ui->note7->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note7->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
    }
    ui->note0->repaint();
    QThread::currentThread()->msleep(displayDuration);
    ui->note0->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note1->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note2->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note3->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note4->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note5->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note6->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note7->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");

    // m_Microphone->start();
    ui->lb_arrow->move(800, 100);
    qDebug() << "Keyboard value heard: " << kbValue;
    // QThread::msleep(displayDuration);
    stopSound();
    if(nPos < 21 and orientationFlag)
    {
        play_next_note();
    }
    if(nPos < 20 and !orientationFlag)
    {
        play_next_note();
    }
}

void Widget::stopSound()
{
    qDebug() << "stop Sound...";
    if(orientationFlag and nPos == 21)
    {
        m_timer->stop();
        MicThread.exit();
        SpeakerThread.exit();
        SpeakerThread.start();

        orientationFlag = false;
        m_audioSource->suspend();
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Orientarion Complete", "Continue?",
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            qDebug() << "continuing...";
            ui->lb_info->setText("Started Lesson\nfrom random notes");
            ui->lb_curr_activity->setText("Lesson");
            m_audioSource->resume();
            playedCnt = 0;
            goodCnt = 0;
            nPos = 0;
            MicThread.start();
        } else {
            qDebug() << "No was clicked";
            QApplication::quit();
        }
    }
    if(!orientationFlag and nPos == 20)
    {
        m_timer->stop();
        MicThread.exit();
        SpeakerThread.exit();
        SpeakerThread.start();
        m_audioSource->suspend();
        ui->lb_info->setText("TO GO TO THE\nNEXT LESSON\na test score of 100%\nor\n90% ave of last 5 tests of 20");
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Lesson Complete", "Continue?",
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            qDebug() << "continuing...";
            m_audioSource->resume();
            playedCnt = 0;
            goodCnt = 0;
            nPos = 0;

            qDebug() << "test complete";
            qDebug() << "testIndex value: " << curLessonInt;
            MicThread.start();
            getNextLesson(currentlesson.toInt());
        } else {
            qDebug() << "No was clicked";
            QApplication::quit();
        }
    }
}

void Widget::getNextLesson(int indexVal)
{
    m_Speaker->clearBuffer();
    qDebug() << "m_buffer" << m_Speaker->m_buffer;
    m_audioOutput->suspend();
    curLessonInt = indexVal + 1;
    currentlesson = QString::number(curLessonInt);
    kbNotePlayLists.clear();
    gNote.clear();
    gKey.clear();
    gTestGroup.clear();
    noteFiles.clear();
    testNotes.clear();
    rawRecArrays.clear();
    // update config to new lesson
    FileLoader::updateConfigLesson(curLessonInt);
    qDebug() << "testIndex value: " << curLessonInt;
    FileLoader files;
    // get sound array set
    FileLoader::ReadLesson(); //rebuild lists
    tonicNote = tonic_map[gNote[curLessonInt]];
    files.GetFileList(tonicNote);
    buildkbNotePlayList(tonicNote);
    qDebug() << "tonicNote = " << tonicNote;
    qDebug() << "gNote = " << gNote[curLessonInt];
    FileLoader::GetRandomTestSet(gTestGroup[curLessonInt]);
    qDebug() << "gTestGroup values: " << gTestGroup[curLessonInt];
    qDebug() <<  "gTestGroup = " << gTestGroup;
    QString temp = "Lesson #" + currentlesson + " Key " + gKey[curLessonInt]
                   + " Test Notes " + gTestGroup[curLessonInt];
    ui->lb_title->setText(temp);
    orientationFlag=true;
    playedCnt = 0;
    goodCnt = 0;
    nPos = 0;
    ui->lb_info->setText("up scale 1 to 7\n octaves 3 times\ndown scale 7 to 1");
    m_audioOutput->resume();
}

void Widget::on_sldDisplayTime_valueChanged(int value)
{
    qDebug() << "--->value = " << value;
    displayDuration = value*1000;
    ui->lb_DurationPos->setText(QString::number(value));
}


void Widget::on_sldTimeoutDuration_valueChanged(int value)
{
    qDebug() << "--->value = " << value;
    playDuration = value*1000;
    ui->lb_TimeoutPos->setText(QString::number(value));
}


void Widget::on_btnTunerON_clicked()
{
    qDebug() << "--->turn starts here ";
}

