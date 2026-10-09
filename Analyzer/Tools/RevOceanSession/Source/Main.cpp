// Rev OCEAN Session: hosts one instance of the reference plug-in with its own editor on screen, so that
// its owner can choose by hand what the host interface cannot set, and then runs capture jobs inside that
// same instance. See README.md for the boundary it keeps and for the formats of its files.

#include "Native.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <csignal>
#include <iostream>
#include <optional>

namespace
{

/** Set by SIGTERM and SIGINT, so that `kill` and Ctrl-C end a session as closing its window does. */
volatile std::sig_atomic_t terminationSignalled = 0;

void noteTermination (int)
{
    terminationSignalled = 1;
}

constexpr int margin = 12;
constexpr int headerHeight = 112;
constexpr int minimumWidth = 760;
constexpr double secondsToWaitForTheWorker = 20.0;

juce::String minutesAndSeconds (double seconds)
{
    const auto whole = juce::roundToInt (std::floor (std::max (0.0, seconds)));
    return juce::String (whole / 60).paddedLeft ('0', 2) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

/** The instruction, the Start button and the state of the session above the plug-in's own editor. */
class SessionView final : public juce::Component
{
public:
    SessionView (revocean::Session& sessionToShow, std::unique_ptr<juce::AudioProcessorEditor> editorToShow,
                 std::function<void()> startPressed)
        : session (sessionToShow), editor (std::move (editorToShow))
    {
        instruction.setText (session.getConfig().instruction, juce::dontSendNotification);
        instruction.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        instruction.setMinimumHorizontalScale (1.0f);
        status.setFont (juce::FontOptions (14.0f));
        status.setJustificationType (juce::Justification::topLeft);
        start.onClick = std::move (startPressed);

        addAndMakeVisible (instruction);
        addAndMakeVisible (status);
        addAndMakeVisible (start);
        addAndMakeVisible (*editor);
        fitToEditor();
    }

    void refresh()
    {
        const auto& config = session.getConfig();
        start.setEnabled (! session.hasStarted());
        start.setButtonText (session.hasStarted() ? "Started" : "Start");
        status.setText ("Demo time left: " + minutesAndSeconds (revocean::demoMinutes * 60.0 - session.secondsSinceCreation())
                            + " of " + minutesAndSeconds (revocean::demoMinutes * 60.0)
                            + ", counted from " + session.createdAt().formatted ("%H:%M:%S")
                            + ", when the instance was created. The session stops by itself in "
                            + minutesAndSeconds (config.stopAfterMinutes * 60.0 - session.secondsSinceCreation()) + ".\n"
                            + session.phase() + " "
                            + juce::String (static_cast<double> (session.framesProcessed()) / config.sampleRate, 1)
                            + " s of audio processed at " + juce::String (juce::roundToInt (config.sampleRate)) + " Hz.",
                        juce::dontSendNotification);
    }

    void resized() override
    {
        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin);
        auto top = header.removeFromTop (44);
        start.setBounds (top.removeFromRight (120).reduced (0, 4));
        instruction.setBounds (top.withTrimmedRight (margin));
        status.setBounds (header.withTrimmedTop (4));
        editor->setTopLeftPosition ((getWidth() - editor->getWidth()) / 2, headerHeight);
    }

    void childBoundsChanged (juce::Component* child) override
    {
        if (child == editor.get())
            fitToEditor();
    }

private:
    void fitToEditor()
    {
        setSize (std::max (minimumWidth, editor->getWidth()), headerHeight + editor->getHeight());
    }

    revocean::Session& session;
    juce::Label instruction, status;
    juce::TextButton start;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

/** The one window of a session. Closing it ends the session. */
class SessionWindow final : public juce::DocumentWindow,
                            private juce::Timer
{
public:
    SessionWindow (revocean::Session& sessionToShow, std::unique_ptr<juce::AudioProcessorEditor> editor,
                   std::optional<double> startAfterSeconds)
        : juce::DocumentWindow (sessionToShow.getConfig().title,
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          session (sessionToShow),
          startAfter (startAfterSeconds),
          view (session, std::move (editor), [this] { session.start(); })
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&view, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        session.onWorkerFinished = [] { juce::JUCEApplicationBase::quit(); };
        session.windowShown (revocean::nativeWindowNumber (*this));
        timerCallback();
        startTimerHz (5);
    }

    ~SessionWindow() override
    {
        stopTimer();
        session.onWorkerFinished = nullptr;
    }

    void closeButtonPressed() override
    {
        session.requestStop ("window_closed");
        timerCallback();
    }

private:
    void timerCallback() override
    {
        if (startAfter.has_value() && shownFor() >= *startAfter)
            session.start();

        if (session.secondsSinceCreation() >= session.getConfig().stopAfterMinutes * 60.0)
            session.requestStop ("demo_margin");

        if (terminationSignalled != 0)
            session.requestStop ("terminated");

        if (session.stopReason().isNotEmpty())
        {
            if (! session.hasStarted())
                juce::JUCEApplicationBase::quit();
            else if (session.secondsSinceStopRequest() > secondsToWaitForTheWorker)
                session.abandon ("the plug-in did not return from processing");
        }

        view.refresh();
    }

    double shownFor() const
    {
        return (juce::Time::getMillisecondCounterHiRes() - shownTicks) / 1000.0;
    }

    revocean::Session& session;
    const std::optional<double> startAfter;
    const double shownTicks = juce::Time::getMillisecondCounterHiRes();
    SessionView view;
};

class SessionApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        revocean::stayAwake();
        std::signal (SIGTERM, noteTermination);
        std::signal (SIGINT, noteTermination);

        if (const auto opened = open(); opened.failed())
        {
            std::cerr << "error: " << opened.getErrorMessage() << std::endl;
            setApplicationReturnValue (1);
            quit();
        }
    }

    void shutdown() override
    {
        window.reset();    // the editor goes before its instance
        session.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->closeButtonPressed();
        else
            quit();
    }

private:
    juce::Result open()
    {
        const auto arguments = getCommandLineParameterArray();
        const auto folderIndex = arguments.indexOf ("--session");
        const auto startIndex = arguments.indexOf ("--start-after");
        if (folderIndex < 0 || folderIndex + 1 >= arguments.size()
            || (startIndex >= 0 && (startIndex + 1 >= arguments.size()
                                    || ! arguments[startIndex + 1].containsOnly ("0123456789."))))
            return juce::Result::fail ("usage: \"Rev OCEAN Session\" --session <folder with session.json> [--start-after <seconds>]");

        const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (arguments[folderIndex + 1]);
        revocean::Config config;
        if (const auto read = revocean::Config::read (folder, config); read.failed())
            return read;

        session = std::make_unique<revocean::Session> (config);
        if (const auto opened = session->open(); opened.failed())
            return opened;

        std::unique_ptr<juce::AudioProcessorEditor> editor (session->createEditor());
        if (editor == nullptr)
            return session->fail ("the plug-in has no editor");

        window = std::make_unique<SessionWindow> (*session, std::move (editor),
                                                  startIndex >= 0 ? std::optional (arguments[startIndex + 1].getDoubleValue())
                                                                  : std::nullopt);
        std::cout << "session " << folder.getFullPathName() << " is open" << std::endl;
        return juce::Result::ok();
    }

    std::unique_ptr<revocean::Session> session;
    std::unique_ptr<SessionWindow> window;
};

} // namespace

START_JUCE_APPLICATION (SessionApplication)
