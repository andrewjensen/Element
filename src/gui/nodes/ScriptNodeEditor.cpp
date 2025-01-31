/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.
    - Author Michael Fisher <mfisher@kushvie.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "kv/lua/object.hpp"
#include "gui/nodes/ScriptNodeEditor.h"
#include "gui/LookAndFeel.h"
#include "scripting/LuaBindings.h"
#include "scripting/ScriptingEngine.h"
#include "scripting/ScriptManager.h"
#include "scripting/Script.h"

namespace Element {

//==============================================================================
static CodeEditorComponent::ColourScheme luaColors()
{
    static const CodeEditorComponent::ColourScheme::TokenType types[] =
    {
         { "Error",          Colour (0xffcc0000) },
         { "Comment",        Colour (0xff6a9955) },
         { "Keyword",        Colour (0xff569cd6) },
         { "Operator",       Colour (0xffb3b3b3) },
         { "Identifier",     Colour (0xffc5c5c5) },
         { "Integer",        Colour (0xffb5cea8) },
         { "Float",          Colour (0xffb5cea8) },
         { "String",         Colour (0xffce9178) },
         { "Bracket",        Colour (0xffd4d4d4) },
         { "Punctuation",    Colour (0xffb3b3b3) },
         { "Preprocessor Text", Colour (0xffc586c0) } // used for control statements
    };

    CodeEditorComponent::ColourScheme cs;

    for (auto& t : types)
        cs.set (t.name, Colour (t.colour));

    return cs;
}

//==============================================================================
class ControlPort : private ParameterListener
{
public:
    ControlPort (Parameter* parameter)
        : ParameterListener (parameter)
    {
        param = parameter;
        control = dynamic_cast<ControlPortParameter*> (param.get());
    }

    virtual ~ControlPort()
    {
        param = nullptr;
        control = nullptr;
    }

    float getValue() const { return param->getValue(); }
    void setValue (float val) { param->setValue (val); }
    
    bool isControl() const  { return control != nullptr; }

    float getControl() const
    {
        if (control)
            return control->get();
        return param->getValue();
    }

    void setControl (float val)
    {
        if (control)
            control->set (val);
        else
            param->setValueNotifyingHost (val);
    }

    std::function<void()> onValueChange;

private:
    Parameter::Ptr param;
    ControlPortParameter::Ptr control;
    void handleNewParameterValue() override
    {
        if (onValueChange)
            onValueChange();
    }
};

class ScriptNodeControlPort : public ControlPort
{
public:
    ScriptNodeControlPort (Parameter* param)
        : ControlPort (param)
    {
        onValueChange = [this]() {
            if (changed.valid())
                changed();
        };
    }

    ~ScriptNodeControlPort() override {}

    sol::function getChangedFunction () const { return changed; }
    void setChangedFunction (const sol::function& f) { changed = f; }

private:
    sol::function changed;
};

//==============================================================================
class LuaNodeParameterPropertyFloat : public PropertyComponent,
                                      private ParameterListener
{
public:
    LuaNodeParameterPropertyFloat (Parameter::Ptr p)
        : PropertyComponent (p->getName (1024)),
          ParameterListener (p),
          param (p)
    {
        if (param->getLabel().isNotEmpty())
        {
            auto name = getName();
            name << " (" << param->getLabel() << ")";
            setName (name);
        }

        addAndMakeVisible (slider);
        slider.setRange (0.0, 1.0, 0.0);
        slider.setSkewFactor (1.0, false);
        slider.setSliderStyle (Slider::LinearBar);

        slider.onDragStart = [this]()
        {
            dragging = true;
            param->beginChangeGesture();
        };

        slider.onDragEnd = [this]()
        {
            dragging = false;
            param->endChangeGesture();
        };

        slider.onValueChange = [this]
        {
            auto newValue = (float) slider.getValue();
            if (param->getValue() != newValue)
            {
                if (! dragging)
                    param->beginChangeGesture();

                param->setValueNotifyingHost (newValue);

                if (! dragging)
                    param->endChangeGesture();
            }
        };

        slider.valueFromTextFunction = [this](const String& text) -> double
        {
            if (auto* cp = dynamic_cast<ControlPortParameter*> (param.get()))
                return (double) cp->convertTo0to1 (text.getFloatValue());
            return text.getDoubleValue();
        };

        slider.textFromValueFunction = [this](double value) -> String
        {
            String text;
            if (auto* cp = dynamic_cast<ControlPortParameter*> (param.get()))
                return cp->getText (static_cast<float> (value), 1024);
            return String (value, 6);
        };

        refresh();
        slider.updateText();
    }

    void refresh() override
    {
        if (static_cast<float> (slider.getValue()) != param->getValue())
            slider.setValue (param->getValue(), dontSendNotification);
    }

private:
    Slider slider;
    Parameter::Ptr param;
    bool dragging = false;

    void handleNewParameterValue() override
    {
        jassert (MessageManager::getInstance()->isThisTheMessageThread());
        if (! dragging)
            slider.setValue (param->getValue(), dontSendNotification);
    }
};

//==============================================================================
class ScriptNodeEditor::CodeEditor : public CodeEditorComponent
{
public:
    CodeEditor (ScriptNodeEditor& o, CodeDocument& doc, CodeTokeniser* tokens)
        : CodeEditorComponent (doc, tokens),
          owner (o)
    {
        setTabSize (4, true);
        setColourScheme (luaColors());
        setFont (getFont().withHeight (getDefaultFontHeight()));
    }

    ~CodeEditor() override {}

    //==========================================================================
    /** Returns the default font height used by this editor */
    float getDefaultFontHeight() const { return defaultFontHeight; }

    //==========================================================================
    void addPopupMenuItems (PopupMenu &menu, const MouseEvent *event) override
    {
        menu.addItem (50001, "Open File");
        menu.addItem (50002, "Save File");
        menu.addSeparator();
        CodeEditorComponent::addPopupMenuItems (menu, event);
    }

    void performPopupMenuAction (int menuItemID) override
    {
        switch (menuItemID)
        {
            case 50001:
            {
                owner.chooser.reset (new FileChooser (
                    "Open script", ScriptManager::getUserScriptsDir(), 
                    "*.lua", false, false, &owner));

                FileChooser& fc (*owner.chooser);
                if (fc.browseForFileToOpen())
                {
                    auto& doc = getDocument();
                    doc.replaceAllContent (
                        fc.getResult().loadFileAsString());
                }
                break;
            }
            
            case 50002:
            {
                 owner.chooser.reset (new FileChooser (
                    "Save script", ScriptManager::getUserScriptsDir(), 
                    "*.lua", false, false, &owner));
                FileChooser& fc (*owner.chooser);
                if (fc.browseForFileToSave (true))
                {
                    TemporaryFile tmpFile (fc.getResult());
                    auto stream = tmpFile.getFile().createOutputStream();
                    if (getDocument().writeToStream (*stream))                
                        tmpFile.overwriteTargetFileWithTemporary();
                }
                break;
            }

            default:
                CodeEditorComponent::performPopupMenuAction (menuItemID);
                break;
        }
    }

private:
    ScriptNodeEditor& owner;
   #if JUCE_MAC
    static constexpr float defaultFontHeight = 14.5f;
   #elif JUCE_WINDOWS
    static constexpr float defaultFontHeight = 13.f;
   #elif JUCE_LINUX
    static constexpr float defaultFontHeight = 16.f;
   #else
    static constexpr float defaultFontHeight = 15.f;
   #endif
};

static ValueTree getUIChild (const Node& node, const String& name) {
    auto UI = node.getUIValueTree();
    return UI.getOrCreateChildWithName (name, nullptr);
}

static ValueTree getScriptNodeEditorState (const Node& node) {
    return getUIChild (node, "ScriptNodeEditor");
}

//==============================================================================
ScriptNodeEditor::ScriptNodeEditor (ScriptingEngine& scripts, const Node& node)
    : NodeEditorComponent (node),
      engine (scripts),
      state (engine.getLuaState()),
      env (state, sol::create, state.globals()),
      fileBrowser (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, 
                   ScriptManager::getUserScriptsDir(), nullptr, nullptr)
{
    setOpaque (true);

    auto M = state.create_table();
    M.new_usertype<ScriptNodeControlPort> ("ControlPort", sol::no_constructor,
        "value",        sol::overload (
            [](ScriptNodeControlPort& self) -> double {
                return self.getValue();
            },
            [](ScriptNodeControlPort& self, bool normal) -> double {
                return normal ? self.getValue() : self.getControl();
            },
            [](ScriptNodeControlPort& self, double value) -> double {
                self.setValue (static_cast<float> (value));
                return self.getValue();
            },
            [](ScriptNodeControlPort& self, double value, bool normal) -> double {
                if (normal) self.setValue (static_cast<float> (value));
                else        self.setControl (static_cast<float> (value));
                return normal ? self.getValue() : self.getControl();
            }
        ),
        "normalized",   sol::property (&ScriptNodeControlPort::getValue,
                                       [](ScriptNodeControlPort& self, double  value) { self.setValue (value); }),
        "regular",      sol::property (&ScriptNodeControlPort::getControl,
                                       [](ScriptNodeControlPort& self, double  value) { self.setControl (value); }),
        "valuechanged", sol::property (&ScriptNodeControlPort::getChangedFunction,
                                       &ScriptNodeControlPort::setChangedFunction)
    );
    env["ScriptNodeEditor.ControlPort"] = M;
    
    lua = getNodeObjectOfType<ScriptNode>();
    jassert (lua);

    chooser.reset (new FileChooser ("Script", ScriptManager::getUserScriptsDir(),
                                    "*.lua", false, false, this));

    addAndMakeVisible (compileButton);
    compileButton.setButtonText ("Compile");
    compileButton.onClick = [this]()
    {
        const auto script = lua->getCodeDocument(false).getAllContent();
        auto result = lua->loadScript (script);
        if (! result.wasOk())
        {
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                "Script Error", result.getErrorMessage());
        }
    };

    addAndMakeVisible (paramsButton);
    paramsButton.setButtonText ("Params");
    paramsButton.setColour (TextButton::buttonOnColourId, Colors::toggleBlue);
    paramsButton.onClick = [this]()
    {
        paramsButton.setToggleState (! paramsButton.getToggleState(), dontSendNotification);
        props.setVisible (paramsButton.getToggleState());
        resized();
    };

    addAndMakeVisible (dspButton);
    dspButton.setButtonText ("DSP");
    dspButton.setColour (TextButton::buttonOnColourId, Colors::toggleBlue);
    dspButton.onClick = [this]()
    {
        if (! dspButton.getToggleState())
        {
            dspButton.setToggleState (true, dontSendNotification);
            uiButton.setToggleState (false, dontSendNotification);
            previewButton.setToggleState (false, dontSendNotification);
            updatePreview();
            updateCodeEditor();
        }
    };

    addAndMakeVisible (uiButton);
    uiButton.setButtonText ("UI");
    uiButton.setColour (TextButton::buttonOnColourId, Colors::toggleBlue);
    uiButton.onClick = [this]()
    {
        if (! uiButton.getToggleState())
        {
            dspButton.setToggleState (false, dontSendNotification);
            uiButton.setToggleState (true, dontSendNotification);
            previewButton.setToggleState (false, dontSendNotification);
            updatePreview();
            updateCodeEditor();
        }
    };

    addAndMakeVisible (previewButton);
    previewButton.setButtonText ("Preview");
    previewButton.setColour (TextButton::buttonOnColourId, Colors::toggleBlue);
    previewButton.onClick = [this]()
    {
        if (! previewButton.getToggleState())
        {
            dspButton.setToggleState (false, dontSendNotification);
            uiButton.setToggleState (false, dontSendNotification);
            previewButton.setToggleState (true, dontSendNotification);
            updateCodeEditor();
            updatePreview();
        }
    };

    addAndMakeVisible (props);
    props.setVisible (paramsButton.getToggleState());

    addAndMakeVisible (console);
    console.setEnvironment (env);

    lua->addChangeListener (this);
    portsChangedConnection = lua->portsChanged.connect (
        std::bind (&ScriptNodeEditor::onPortsChanged, this));

    setSize (660, 480);

    const auto SNE = getScriptNodeEditorState (getNode());
    if ((bool) SNE.getProperty ("showParams", false))
    {
        paramsButton.setToggleState (true, dontSendNotification);
        props.setVisible (true);
    }

    if ((bool) SNE.getProperty ("showDSP", true))
        dspButton.setToggleState (true, dontSendNotification);
    else if ((bool) SNE.getProperty ("showUI", false))
        uiButton.setToggleState (true, dontSendNotification);
    else if ((bool) SNE.getProperty ("preview", false))
        previewButton.setToggleState (true, dontSendNotification);
    else
        dspButton.setToggleState (true, dontSendNotification);

    updateAll();
    updatePreview();
    resized();
}

ScriptNodeEditor::~ScriptNodeEditor()
{
    portsChangedConnection.disconnect();
    lua->removeChangeListener (this);

    auto SNE = getScriptNodeEditorState (getNode());
    SNE.setProperty ("showParams", paramsButton.getToggleState(), nullptr)
       .setProperty ("console",    console.isVisible(), nullptr)
       .setProperty ("showDSP",    dspButton.getToggleState(), nullptr)
       .setProperty ("showUI",     uiButton.getToggleState(), nullptr)
       .setProperty ("preview",    previewButton.getToggleState(), nullptr);

    editor.reset();
}

//==============================================================================
sol::table ScriptNodeEditor::createContext()
{
    using CPP = ControlPortParameter;
    sol::state_view view (state.lua_state());
    sol::table ctx  = view.create_table();

    ctx["params"] = view.create_table();
    for (auto* param : lua->getParameters())
    {
        ctx["params"][1 + param->getParameterIndex()] = 
            std::make_shared<ScriptNodeControlPort> (param);
    }

    return ctx;
}

//==============================================================================
void ScriptNodeEditor::updateAll()
{
    updateCodeEditor();
    updateProperties();
}

void ScriptNodeEditor::updateCodeEditor()
{
    editor.reset (new CodeEditor (*this, getActiveDoc(), &tokens));
    addAndMakeVisible (editor.get());
    resized();
}

void ScriptNodeEditor::updateProperties()
{
    props.clear();
    Array<PropertyComponent*> pcs;
    for (auto* param : lua->getParameters())
    {
        if (! param->isAutomatable())
            continue;
        pcs.add (new LuaNodeParameterPropertyFloat (param));
    }
    props.addProperties (pcs);
}

void ScriptNodeEditor::updatePreview()
{
    if (previewButton.getToggleState())
    {
        try {
            Script loader (state);
            if (loader.load (lua->getCodeDocument(true).getAllContent()))
            {
                auto f = loader.caller(); env.set_on (f);
                auto ctx = createContext();
                sol::protected_function_result instance = f (ctx);
            
                if (! instance.valid())
                {
                    sol::error e = instance;
                    for (const auto& line : StringArray::fromLines (e.what()))
                        console.addText (line);
                    return;
                }
                
                if (instance.get_type() == sol::type::table)
                {
                    sol::table DSPUI = instance;
                    sol::table editor;
                    
                    switch (DSPUI["editor"].get_type())
                    {
                        case sol::type::function:
                        {
                            sol::function instantiate = DSPUI["editor"];
                            auto editorResult = instantiate (ctx);
                            if (! editorResult.valid())
                            {
                                sol::error e = editorResult;
                                for (const auto& line : StringArray::fromLines (e.what()))
                                    console.addText (line);
                            }
                            else if (editorResult.get_type() == sol::type::table)
                            {
                                editor = editorResult;
                            }
                            break;
                        }
                        default:
                            break;
                    }

                    if (auto* const c = kv::lua::object_userdata<Component> (editor))
                    {
                        comp = c;
                        widget = editor;
                        addAndMakeVisible (*comp);
                        comp->setAlwaysOnTop (true);
                    }
                    else
                    {
                        console.addText ("ScriptNodeEditor: didn't get widget from DSPUI script");
                    }
                }
            }
            else
            {
                console.addText (loader.getErrorMessage());
            }
        }
        catch (const std::exception& e)
        {
            for (const auto& line : StringArray::fromLines (e.what()))
                console.addText (line);
        }
    }
    else
    {
        if (comp != nullptr)
        {
            removeChildComponent (comp);
            comp = nullptr;
            widget = sol::lua_nil;
        }
    }

    editor->setVisible (! previewButton.getToggleState());
    resized();
}

void ScriptNodeEditor::updateScriptsCombo()
{
    // scriptsCombo.clear (dontSendNotification);
}

void ScriptNodeEditor::onPortsChanged()
{
    updateProperties();
}

CodeDocument& ScriptNodeEditor::getActiveDoc()
{
    return lua->getCodeDocument (uiButton.getToggleState());
}

void ScriptNodeEditor::changeListenerCallback (ChangeBroadcaster*)
{
    updateAll();
    resized();
}

void ScriptNodeEditor::paint (Graphics& g)
{
    g.fillAll (Element::LookAndFeel::widgetBackgroundColor.darker());
}

void ScriptNodeEditor::resized()
{
    const int toolbarSize = 22;
    auto r1 = getLocalBounds().reduced (4);
    auto r2 = r1.removeFromTop (toolbarSize);
    
    fileBrowser.setBounds (r1.reduced (8));

    dspButton.changeWidthToFitText (r2.getHeight());
    dspButton.setBounds (r2.removeFromLeft (dspButton.getWidth()));
    r2.removeFromLeft (2);
    uiButton.changeWidthToFitText (r2.getHeight());
    uiButton.setBounds (r2.removeFromLeft (uiButton.getWidth()));
    r2.removeFromLeft (2);
    previewButton.changeWidthToFitText (r2.getHeight());
    previewButton.setBounds (r2.removeFromLeft (previewButton.getWidth()));
    r2.removeFromLeft (2);
    compileButton.changeWidthToFitText (r2.getHeight());
    compileButton.setBounds (r2.removeFromLeft (compileButton.getWidth()));
    
    paramsButton.changeWidthToFitText (r2.getHeight());
    paramsButton.setBounds (r2.removeFromRight (paramsButton.getWidth()));

    r1.removeFromTop (2);

    if (props.isVisible())
    {
        props.setBounds (r1.removeFromRight (220));
        r1.removeFromRight (2);
    }

    console.setBounds (r1.removeFromBottom (roundToInt ((float)getHeight() * (1.0 / 3.0))));
    if (editor)
        editor->setBounds (r1);

    if (previewButton.getToggleState() && comp != nullptr)
    {
        auto b = comp->getLocalBounds();
        comp->setBounds (b.withCentre (r1.getCentre()));
    }
}

}
