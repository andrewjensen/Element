/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.

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

#include "controllers/AppController.h"
#include "controllers/EngineController.h"

#include "engine/VelocityCurve.h"

#include "gui/properties/MidiMultiChannelPropertyComponent.h"
#include "gui/GuiCommon.h"
#include "gui/views/GraphSettingsView.h"

#include "ScopedFlag.h"

namespace Element {
    typedef Array<PropertyComponent*> PropertyArray;
    
    class MidiChannelPropertyComponent : public ChoicePropertyComponent
    {
    public:
        MidiChannelPropertyComponent (const String& name = "MIDI Channel")
            : ChoicePropertyComponent (name)
        {
            choices.add ("Omni");
            choices.add ("");
            for (int i = 1; i <= 16; ++i)
            {
                choices.add (String (i));
            }
        }
        
        /** midi channel.  0 means omni */
        inline int getMidiChannel() const { return midiChannel; }
        
        inline int getIndex() const override
        {
            const int index = midiChannel == 0 ? 0 : midiChannel + 1;
            return index;
        }
        
        inline void setIndex (const int index) override
        {
            midiChannel = (index <= 1) ? 0 : index - 1;
            jassert (isPositiveAndBelow (midiChannel, 17));
            midiChannelChanged();
        }
        
        virtual void midiChannelChanged() { }
        
    protected:
        int midiChannel = 0;
    };
    
    class RenderModePropertyComponent : public ChoicePropertyComponent
    {
    public:
        RenderModePropertyComponent (const Node& g, const String& name = "Rendering Mode")
            : ChoicePropertyComponent (name), graph(g)
        {
            jassert(graph.isRootGraph());
            choices.add ("Single");
            choices.add ("Parallel");
        }
        
        inline int getIndex() const override
        {
            const String slug = graph.getProperty (Tags::renderMode, "single").toString();
            return (slug == "single") ? 0 : 1;
        }
        
        inline void setIndex (const int index) override
        {
            if (! locked)
            {
                RootGraph::RenderMode mode = index == 0 ? RootGraph::SingleGraph : RootGraph::Parallel;
                graph.setProperty (Tags::renderMode, RootGraph::getSlugForRenderMode (mode));
                if (auto* node = graph.getGraphNode ())
                    if (auto* root = dynamic_cast<RootGraph*> (node->getAudioProcessor()))
                        root->setRenderMode (mode);
            }
            else
            {
                refresh();
            }
        }
        
    protected:
        Node graph;
        bool locked = false;
    };

    class VelocityCurvePropertyComponent : public ChoicePropertyComponent
    {
    public:
        VelocityCurvePropertyComponent (const Node& g)
            : ChoicePropertyComponent ("Velocity Curve"),
              graph (g)
        {
            for (int i = 0; i < VelocityCurve::numModes; ++i)
                choices.add (VelocityCurve::getModeName (i));
        }

        inline int getIndex() const override
        {
            return graph.getProperty ("velocityCurveMode", (int) VelocityCurve::Linear);
        }
        
        inline void setIndex (const int i) override
        {
            if (! isPositiveAndBelow (i, (int) VelocityCurve::numModes))
                return;
            
            graph.setProperty ("velocityCurveMode", i);
            
            if (auto* obj = graph.getGraphNode())
                if (auto* proc = dynamic_cast<RootGraph*> (obj->getAudioProcessor()))
                    proc->setVelocityCurveMode ((VelocityCurve::Mode) i);
        }

    private:
        Node graph;
        int index;
    };

    class RootGraphMidiChannels : public MidiMultiChannelPropertyComponent
    {
    public:
        RootGraphMidiChannels (const Node& g, int proposedWidth)
            : graph (g) 
        {
            setSize (proposedWidth, 10);
            setChannels (g.getMidiChannels().get());
            changed.connect (std::bind (&RootGraphMidiChannels::onChannelsChanged, this));
        }

        ~RootGraphMidiChannels()
        {
            changed.disconnect_all_slots();
        }

        void onChannelsChanged()
        {
            if (graph.isRootGraph())
                if (auto* node = graph.getGraphNode())
                    if (auto *proc = dynamic_cast<RootGraph*> (node->getAudioProcessor()))
                    { 
                        proc->setMidiChannels (getChannels());
                        graph.setProperty (Tags::midiChannels, getChannels().toMemoryBlock());
                    }
        }

        Node graph;
    };

    class RootGraphMidiChanel : public MidiChannelPropertyComponent
    {
    public:
        RootGraphMidiChanel (const Node& n)
            : MidiChannelPropertyComponent(),
              node (n)
        {
            jassert (node.isRootGraph());
            midiChannel = node.getProperty (Tags::midiChannel, 0);
        }
        
        void midiChannelChanged() override
        {
            auto session = ViewHelpers::getSession (this);
            node.setProperty (Tags::midiChannel, getMidiChannel());
            if (NodeObjectPtr ptr = node.getGraphNode())
                if (auto* root = dynamic_cast<RootGraph*> (ptr->getAudioProcessor()))
                    root->setMidiChannel (getMidiChannel());
        }
        
        Node node;
    };
    
    class MidiProgramPropertyComponent : public SliderPropertyComponent

    {
    public:
        MidiProgramPropertyComponent (const Node& n)
            : SliderPropertyComponent ("MIDI Program", -1.0, 127.0, 1.0, 1.0, false),
              node (n)
        {
            slider.textFromValueFunction = [](double value) -> String {
                const int iValue = static_cast<int> (value);
                if (iValue < 0) 
                    return "None";
                return String (1 + iValue);
            };

            slider.valueFromTextFunction = [](const String& text) -> double {
                if (text == "None")
                    return -1.0;
                return static_cast<double> (text.getIntValue()) - 1.0;
            };

            // needed to ensure proper display when first loaded
            slider.updateText();
        }

        virtual ~MidiProgramPropertyComponent()
        {
            slider.textFromValueFunction = nullptr;
            slider.valueFromTextFunction = nullptr;
        }

        void setLocked (const var& isLocked)
        {
            locked = isLocked;
            refresh();
        }

        void setValue (double v) override
        {
            if (! locked)
            {
                node.setProperty (Tags::midiProgram, roundToInt (v));
                if (NodeObjectPtr ptr = node.getGraphNode())
                    if (auto* root = dynamic_cast<RootGraph*> (ptr->getAudioProcessor()))
                        root->setMidiProgram ((int) node.getProperty (Tags::midiProgram));
            }
            else
            {
                refresh();
            }
        }
        
        double getValue() const override 
        {
            return (double) node.getProperty (Tags::midiProgram, -1);
        }
        
        Node node;
        bool locked;
    };

    class GraphPropertyPanel : public PropertyPanel
    {
    public:
        GraphPropertyPanel() : locked (var (true)) { }
        ~GraphPropertyPanel()
        {
            clear();
        }
        
        void setLocked (const bool isLocked)
        {
            locked = isLocked;
        }

        void setNode (const Node& newNode)
        {
            clear();
            graph = newNode;
            if (graph.isValid() && graph.isGraph())
            {
                PropertyArray props;
                getSessionProperties (props, graph);
                if (useHeader)
                    addSection ("Graph Settings", props);
                else
                    addProperties (props);
            }
        }
        
        void setUseHeader (bool header)
        {
            if (useHeader == header)
                return;
            useHeader = header;
            setNode (graph);
        }

    private:
        Node graph;
        var locked;
        bool useHeader = true;

        static void maybeLockObject (PropertyComponent* p, const var& locked)
        {
            ignoreUnused (p, locked);
        }

        void getSessionProperties (PropertyArray& props, Node g)
        {
            props.add (new TextPropertyComponent (g.getPropertyAsValue (Slugs::name),
                                                  TRANS("Name"), 256, false));
           #if defined (EL_PRO)
            props.add (new RenderModePropertyComponent (g));
            props.add (new VelocityCurvePropertyComponent (g));
           #endif

           #if defined (EL_SOLO) || defined (EL_PRO)
            props.add (new RootGraphMidiChannels (g, getWidth() - 100));
           #else
            props.add (new RootGraphMidiChanel (g));
           #endif

           #if defined (EL_PRO)
            props.add (new MidiProgramPropertyComponent (g));
           #endif

            for (auto* const p : props)
                maybeLockObject (p, locked);
            
            // props.add (new BooleanPropertyComponent (g.getPropertyAsValue (Tags::persistent),
            //                                          TRANS("Persistent"),
            //                                          TRANS("Don't unload when deactivated")));
        }
    };
    
    GraphSettingsView::GraphSettingsView()
    {
        setName ("GraphSettings");
        addAndMakeVisible (props = new GraphPropertyPanel());
        addAndMakeVisible (graphButton);
        graphButton.setTooltip ("Show graph editor");
        graphButton.addListener (this);
        setEscapeTriggersClose (true);

        activeGraphIndex.addListener (this);
    }
    
    GraphSettingsView::~GraphSettingsView()
    {
        activeGraphIndex.removeListener (this);
    }
    
    void GraphSettingsView::setPropertyPanelHeaderVisible (bool useHeader)
    {
        props->setUseHeader (useHeader);
    }

    void GraphSettingsView::setGraphButtonVisible (bool isVisible)
    {
        graphButton.setVisible (isVisible);
        resized();
        repaint();
    }

    void GraphSettingsView::didBecomeActive()
    {
        if (isShowing())
            grabKeyboardFocus();
        stabilizeContent();
    }
    
    void GraphSettingsView::stabilizeContent()
    {
        if (auto* const world = ViewHelpers::getGlobals (this))
        {
            props->setNode (world->getSession()->getCurrentGraph());
        }
   
        if (auto session = ViewHelpers::getSession (this))
        {
            if (! activeGraphIndex.refersToSameSourceAs (session->getActiveGraphIndexObject ()))
            {
                ScopedFlag flag (updateWhenActiveGraphChanges, false);
                activeGraphIndex.referTo (session->getActiveGraphIndexObject ());
            }
        }
    }
    
    void GraphSettingsView::resized()
    {
        props->setBounds (getLocalBounds().reduced (2));
        const int configButtonSize = 14;
        graphButton.setBounds (getWidth() - configButtonSize - 4, 4, 
                                configButtonSize, configButtonSize);
    }

    void GraphSettingsView::buttonClicked (Button* button)
    {
        if (button == &graphButton)
            if (auto* const world = ViewHelpers::getGlobals (this))
                world->getCommandManager().invokeDirectly (Commands::showGraphEditor, true);
    }

    void GraphSettingsView::setUpdateOnActiveGraphChange (bool shouldUpdate)
    {
        if (updateWhenActiveGraphChanges == shouldUpdate)
            return;
        updateWhenActiveGraphChanges = shouldUpdate;
    }

    void GraphSettingsView::valueChanged (Value& value)
    {
        if (updateWhenActiveGraphChanges && value.refersToSameSourceAs (value))
            stabilizeContent();
    }
}
