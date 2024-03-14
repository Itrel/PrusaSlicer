///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 André Althaus
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
//#include "stdlib.h"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Layer.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#if ENABLE_OPENGL_AUTO_AA_SAMPLES
#include "GUI_Init.hpp"
#endif // ENABLE_OPENGL_AUTO_AA_SAMPLES
#include "I18N.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "OpenGLManager.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "DoubleSlider.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "NotificationManager.hpp"

#ifdef _WIN32
#include "BitmapComboBox.hpp"
#endif

namespace Slic3r {
namespace GUI {

View3D::View3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

View3D::~View3D()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool View3D::init(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

#if ENABLE_OPENGL_AUTO_AA_SAMPLES
    const GUI_InitParams* const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this, (init_params != nullptr) ? init_params->opengl_aa : false);
#else
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
#endif // ENABLE_OPENGL_AUTO_AA_SAMPLES
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());

    m_canvas->enable_picking(true);
    m_canvas->get_selection().set_mode(Selection::Instance);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    m_canvas->enable_undoredo_toolbar(true);
    m_canvas->enable_labels(true);
    m_canvas->enable_slope(true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        //m_canvas->render();
        m_canvas->set_as_dirty();
}

Preview::Preview(
    wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config,
    BackgroundSlicingProcess* process, GCodeProcessorResult* gcode_result, std::function<void()> schedule_background_process_func)
    : m_config(config)
    , m_process(process)
    , m_gcode_result(gcode_result)
    , m_schedule_background_process(schedule_background_process_func)
{
    if (init(parent, bed, model))
        load_print();
}

void Preview::set_layers_slider_values_range(int bottom, int top)
{
    m_layers_slider->SetHigherValue(std::min(top, m_layers_slider->GetMaxValue()));
    m_layers_slider->SetLowerValue(std::max(bottom, m_layers_slider->GetMinValue()));
}

bool Preview::init(wxWindow* parent, Bed3D& bed, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    // to match the background of the sliders
#ifdef _WIN32 
    wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32 

#if ENABLE_OPENGL_AUTO_AA_SAMPLES
    const GUI_InitParams* const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this, (init_params != nullptr) ? init_params->opengl_aa : false);
#else
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
#endif // ENABLE_OPENGL_AUTO_AA_SAMPLES
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->show_legend(true);
    m_canvas->enable_dynamic_background(true);

    m_layers_slider_sizer = create_layers_slider_sizer();

    wxGetApp().UpdateDarkUI(m_bottom_toolbar_panel = new wxPanel(this));

    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_left_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);
    right_sizer->Add(m_layers_slider_sizer, 1, wxEXPAND, 0);

    m_moves_slider = new DoubleSlider::Control(m_bottom_toolbar_panel, wxID_ANY, 0, 0, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL, "moves_slider");
    m_moves_slider->SetDrawMode(DoubleSlider::dmSequentialGCodeView);

    wxBoxSizer* bottom_toolbar_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_toolbar_sizer->Add(m_moves_slider, 1, wxALL | wxEXPAND, 0);
    m_bottom_toolbar_panel->SetSizer(bottom_toolbar_sizer);

    m_left_sizer->Add(m_bottom_toolbar_panel, 0, wxALL | wxEXPAND, 0);
    m_left_sizer->Hide(m_bottom_toolbar_panel);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_left_sizer, 1, wxALL | wxEXPAND, 0);
    main_sizer->Add(right_sizer, 0, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();
    
    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas != nullptr)
        delete m_canvas;

    if (m_canvas_widget != nullptr)
        delete m_canvas_widget;
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string& direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_gcode_shells()
{
    m_canvas->load_gcode_shells();
}

void Preview::load_print(bool keep_z_range)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range);
    else if (tech == ptSLA)
        load_print_as_sla();

    Layout();
}

void Preview::reload_print()
{
    if (!IsShown())
        return;

    m_loaded = false;
    load_print();
}

void Preview::msw_rescale()
{
    // rescale slider
    if (m_layers_slider != nullptr) m_layers_slider->msw_rescale();
    if (m_moves_slider != nullptr) m_moves_slider->msw_rescale();

    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    // rescale legend
    reload_print();
}

void Preview::sys_color_changed()
{
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);
    wxGetApp().UpdateAllStaticTextDarkUI(m_bottom_toolbar_panel);
#endif // _WIN32

    if (m_layers_slider != nullptr)
        m_layers_slider->sys_color_changed();
}


void Preview::render_imgui_double_slider(GLCanvas3D& canvas)
{
    if (m_layers_slider && m_layers_slider->IsShown())
        m_layers_slider->imgui_render(canvas);
    if (m_moves_slider && m_moves_slider->IsShown() && m_bottom_toolbar_panel->IsShown())
        m_moves_slider->imgui_render(canvas);
}

void Preview::jump_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider) m_layers_slider->OnChar(evt);
}

void Preview::move_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider != nullptr) m_layers_slider->OnKeyDown(evt);
}

void Preview::edit_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider != nullptr) m_layers_slider->OnChar(evt);
}

void Preview::bind_event_handlers()
{
    Bind(wxEVT_SIZE, &Preview::on_size, this);
    m_moves_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_moves_slider_scroll_changed, this);
}

void Preview::unbind_event_handlers()
{
    Unbind(wxEVT_SIZE, &Preview::on_size, this);
    m_moves_slider->Unbind(wxEVT_SCROLL_CHANGED, &Preview::on_moves_slider_scroll_changed, this);
}

void Preview::move_moves_slider(wxKeyEvent& evt)
{
    if (m_moves_slider != nullptr) m_moves_slider->OnKeyDown(evt);
}

void Preview::hide_layers_slider()
{
    m_layers_slider_sizer->Hide((size_t)0);
    Layout();
}

void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

wxBoxSizer* Preview::create_layers_slider_sizer()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    m_layers_slider = new DoubleSlider::Control(this, wxID_ANY, 0, 0, 0, 100, wxDefaultPosition, wxDefaultSize, wxVERTICAL, "layers_slider");

    m_layers_slider->SetDrawMode(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA,
        wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects"));
    m_layers_slider->enable_action_icon(wxGetApp().is_editor());

    sizer->Add(m_layers_slider, 0, wxEXPAND, 0);

    // sizer, m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_layers_slider_from_canvas, this);
    m_canvas_widget->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SHIFT)
            m_layers_slider->UseDefaultColors(true);
        event.Skip();
        });

    m_layers_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_layers_slider_scroll_changed, this);

    Bind(DoubleSlider::wxCUSTOMEVT_TICKSCHANGED, [this](wxEvent&) {
        Model& model = wxGetApp().plater()->model();
        model.custom_gcode_per_print_z = m_layers_slider->GetTicksValues();
        m_schedule_background_process();

        m_keep_current_preview_type = false;
        reload_print();
    });

    return sizer;
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double>& zs, double &z, double eps)
{
    if (zs.empty())
        return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        -- it_l;
        if (z - *it_l < eps)
            return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps)
            return 0;
    } else {
        auto it_l = it_h;
        -- it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) {
            return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin());
        }
    }
    return -1;
}

void Preview::check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model, const std::vector<double>& layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](CustomGCode::Item val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.print_z - DoubleSlider::epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

void Preview::update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    // Save the initial slider span.
    double z_low = m_layers_slider->GetLowerValueD();
    double z_high = m_layers_slider->GetHigherValueD();
    bool   was_empty = m_layers_slider->GetMaxValue() == 0;

    bool force_sliders_full_range = was_empty;
    if (!keep_z_range) {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_layers_slider->GetMaxValueD()) > DoubleSlider::epsilon()/*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool   snap_to_min = force_sliders_full_range || m_layers_slider->is_lower_at_min();
    bool   snap_to_max = force_sliders_full_range || m_layers_slider->is_higher_at_max();

    // Detect and set manipulation mode for double slider
    update_layers_slider_mode();

    Plater* plater = wxGetApp().plater();
    CustomGCode::Info ticks_info_from_model;
    if (wxGetApp().is_editor())
        ticks_info_from_model = plater->model().custom_gcode_per_print_z;
    else {
        ticks_info_from_model.mode = CustomGCode::Mode::SingleExtruder;
        ticks_info_from_model.gcodes = m_gcode_result->custom_gcode_per_print_z;
    }
    check_layers_slider_values(ticks_info_from_model.gcodes, layers_z);

    //first of all update extruder colors to avoid crash, when we are switching printer preset from MM to SM
    m_layers_slider->SetExtruderColors(plater->get_extruder_colors_from_plater_config(wxGetApp().is_editor() ? nullptr : m_gcode_result));
    m_layers_slider->SetSliderValues(layers_z);
    assert(m_layers_slider->GetMinValue() == 0);
    m_layers_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low = 0;
    int idx_high = m_layers_slider->GetMaxValue();
    if (!layers_z.empty()) {
        if (!snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }
    m_layers_slider->SetSelectionSpan(idx_low, idx_high);
    m_layers_slider->SetTicksValues(ticks_info_from_model);

    bool sla_print_technology = plater->printer_technology() == ptSLA;
    bool sequential_print = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects");
    m_layers_slider->SetDrawMode(sla_print_technology, sequential_print);
    if (sla_print_technology)
        m_layers_slider->SetLayersTimes(plater->sla_print().print_statistics().layers_times);
    else
        m_layers_slider->SetLayersTimes(m_canvas->get_gcode_layers_times_cache(), m_gcode_result->print_statistics.modes.front().time);

    // check if ticks_info_from_model contains ColorChange g-code
    bool color_change_already_exists = false;
    for (const CustomGCode::Item& gcode: ticks_info_from_model.gcodes)
        if (gcode.type == CustomGCode::Type::ColorChange) {
            color_change_already_exists = true;
            break;
        }

    // Suggest the auto color change, if model looks like sign
    if (!color_change_already_exists &&
        wxGetApp().app_config->get_bool("allow_auto_color_change") &&
        m_layers_slider->IsNewPrint())
    {
        const Print& print = wxGetApp().plater()->fff_print();

        //bool is_possible_auto_color_change = false;
        for (auto object : print.objects()) {
            double object_x = double(object->size().x());
            double object_y = double(object->size().y());

            // if it's sign, than object have not to be a too height
            double height = object->height();
            coord_t longer_side = std::max(object_x, object_y);
            auto   num_layers = int(object->layers().size());
            if (height / longer_side > 0.3 || num_layers < 2)
                continue;

            const ExPolygons& bottom = object->get_layer(0)->lslices;
            double bottom_area = area(bottom);

            // at least 25% of object's height have to be a solid 
            int  i, min_solid_height = int(0.25 * num_layers);
            for (i = 1; i <= min_solid_height; ++ i) {
                double cur_area = area(object->get_layer(i)->lslices);
                if (!DoubleSlider::equivalent_areas(bottom_area, cur_area)) {
                    // but due to the elephant foot compensation, the first layer may be slightly smaller than the others
                    if (i == 1 && fabs(cur_area - bottom_area) / bottom_area < 0.1) {
                        // So, let process this case and use second layer as a bottom 
                        bottom_area = cur_area;
                        continue;
                    }
                    break;
                }
            }
            if (i < min_solid_height)
                continue;

            if (DoubleSlider::check_color_change(object, i, num_layers, true, [this, object](const Layer*) {
                NotificationManager* notif_mngr = wxGetApp().plater()->get_notification_manager();
                notif_mngr->push_notification(
                    NotificationType::SignDetected, NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                    _u8L("NOTE:") + "\n" +
                    format(_u8L("Sliced object \"%1%\" looks like a logo or a sign"), object->model_object()->name) + "\n",
                    _u8L("Apply color change automatically"),
                    [this](wxEvtHandler*) {
                        m_layers_slider->auto_color_change();
                        return true;
                    });

                notif_mngr->apply_in_preview();
                return true;
            }) )
                // first object with color chnages is found
                break;
        }
    }

    m_layers_slider_sizer->Show((size_t)0);
    Layout();
}

void Preview::update_layers_slider_mode()
{
    //    true  -> single-extruder printer profile OR 
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool    one_extruder_printed_model = true;

    // extruder used for whole model for multi-extruder printer profile
    int     only_extruder = -1; 

    if (wxGetApp().extruders_edited_cnt() > 1)
    {
        const ModelObjectPtrs& objects = wxGetApp().plater()->model().objects;

        // check if whole model uses just only one extruder
        if (!objects.empty())
        {
            const int extruder = objects[0]->config.has("extruder") ?
                                 objects[0]->config.option("extruder")->getInt() : 0;

            auto is_one_extruder_printed_model = [objects, extruder]()
            {
                for (ModelObject* object : objects)
                {
                    if (object->config.has("extruder") &&
                        object->config.option("extruder")->getInt() != extruder)
                        return false;

                    for (ModelVolume* volume : object->volumes)
                        if ((volume->config.has("extruder") && 
                            volume->config.option("extruder")->getInt() != 0 && // extruder isn't default
                            volume->config.option("extruder")->getInt() != extruder) ||
                            !volume->mm_segmentation_facets.empty())
                            return false;

                    for (const auto& range : object->layer_config_ranges)
                        if (range.second.has("extruder") &&
                            range.second.option("extruder")->getInt() != 0 && // extruder isn't default
                            range.second.option("extruder")->getInt() != extruder)
                            return false;
                }
                return true;
            };

            if (is_one_extruder_printed_model())
                only_extruder = extruder;
            else
                one_extruder_printed_model = false;
        }
    }

    m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
}

void Preview::reset_layers_slider()
{
    m_layers_slider->SetHigherValue(0);
    m_layers_slider->SetLowerValue(0);
}

void Preview::update_layers_slider_from_canvas(wxKeyEvent& event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    if (key == 'S' || key == 'W') {
        const int new_pos = key == 'W' ? m_layers_slider->GetHigherValue() + 1 : m_layers_slider->GetHigherValue() - 1;
        m_layers_slider->SetHigherValue(new_pos);
        if (event.ShiftDown() || m_layers_slider->is_one_layer()) m_layers_slider->SetLowerValue(m_layers_slider->GetHigherValue());
    }
    else if (key == 'A' || key == 'D') {
        const int new_pos = key == 'D' ? m_moves_slider->GetHigherValue() + 1 : m_moves_slider->GetHigherValue() - 1;
        m_moves_slider->SetHigherValue(new_pos);
        if (event.ShiftDown() || m_moves_slider->is_one_layer()) m_moves_slider->SetLowerValue(m_moves_slider->GetHigherValue());
    }
    else if (key == 'X')
        m_layers_slider->ChangeOneLayerLock();
    else if (key == WXK_SHIFT)
        m_layers_slider->UseDefaultColors(false);
    else
        event.Skip();
}

void Preview::update_moves_slider(std::optional<int> visible_range_min, std::optional<int> visible_range_max)
{
    if (m_gcode_result->moves.empty())
        return;

    const libvgcode::Interval& range = m_canvas->get_gcode_view_enabled_range();
    uint32_t last_gcode_id = m_canvas->get_gcode_vertex_at(range[0]).gcode_id;
    std::optional<uint32_t> gcode_id_min = visible_range_min.has_value() ?
        std::optional<uint32_t>{ m_canvas->get_gcode_vertex_at(*visible_range_min).gcode_id } : std::nullopt;
    std::optional<uint32_t> gcode_id_max = visible_range_max.has_value() ?
        std::optional<uint32_t>{ m_canvas->get_gcode_vertex_at(*visible_range_max).gcode_id } : std::nullopt;

    const size_t range_size = range[1] - range[0] + 1;
    std::vector<double> values;
    values.reserve(range_size);
    std::vector<double> alternate_values;
    alternate_values.reserve(range_size);

    std::optional<uint32_t> visible_range_min_id;
    std::optional<uint32_t> visible_range_max_id;
    uint32_t counter = 0;

    for (size_t i = range[0]; i <= range[1]; ++i) {
        const uint32_t gcode_id = m_canvas->get_gcode_vertex_at(i).gcode_id;
        bool skip = false;
        if (i > range[0]) {
            // skip consecutive moves with same gcode id (resulting from processing G2 and G3 lines)
            if (last_gcode_id == gcode_id) {
                values.back() = static_cast<double>(i + 1);
                skip = true;
            }
            else
                last_gcode_id = gcode_id;
        }

        if (!skip) {
            values.emplace_back(static_cast<double>(i + 1));
            alternate_values.emplace_back(static_cast<double>(gcode_id));
            if (gcode_id_min.has_value() && alternate_values.back() == static_cast<double>(*gcode_id_min))
                visible_range_min_id = counter;
            else if (gcode_id_max.has_value() && alternate_values.back() == static_cast<double>(*gcode_id_max))
                visible_range_max_id = counter;
            ++counter;
        }
    }

    const int span_min_id = visible_range_min_id.has_value() ? *visible_range_min_id : 0;
    const int span_max_id = visible_range_max_id.has_value() ? *visible_range_max_id : static_cast<int>(values.size()) - 1;

    m_moves_slider->SetSliderValues(values);
    m_moves_slider->SetSliderAlternateValues(alternate_values);
    m_moves_slider->SetMaxValue(static_cast<int>(values.size()) - 1);
    m_moves_slider->SetSelectionSpan(span_min_id, span_max_id);
}

void Preview::enable_moves_slider(bool enable)
{
    bool render_as_disabled = !enable;
    if (m_moves_slider != nullptr && m_moves_slider->is_rendering_as_disabled() != render_as_disabled) {
        m_moves_slider->set_render_as_disabled(render_as_disabled);
        m_moves_slider->Refresh();
    }
}

void Preview::load_print_as_fff(bool keep_z_range)
{
    if (wxGetApp().mainframe == nullptr || wxGetApp().is_recreating_gui())
        // avoid processing while mainframe is being constructed
        return;

    if (m_loaded || m_process->current_printer_technology() != ptFFF)
        return;

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    bool has_layers = false;
    const Print *print = m_process->fff_print();
    if (print->is_step_done(posSlice)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->layers().empty()) {
                has_layers = true;
                break;
            }
    }
	if (print->is_step_done(posSupportMaterial)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->support_layers().empty()) {
                has_layers = true;
                break;
            }
    }

    if (wxGetApp().is_editor() && !has_layers) {
        m_canvas->reset_gcode_layers_times_cache();
        hide_layers_slider();
        m_left_sizer->Hide(m_bottom_toolbar_panel);
        m_left_sizer->Layout();
        Refresh();
        m_canvas_widget->Refresh();
        return;
    }

    libvgcode::EViewType gcode_view_type = m_canvas->get_gcode_view_type();
    const bool gcode_preview_data_valid = !m_gcode_result->moves.empty();
    const bool is_pregcode_preview = !gcode_preview_data_valid && wxGetApp().is_editor();

    const std::vector<std::string> tool_colors = wxGetApp().plater()->get_extruder_colors_from_plater_config(m_gcode_result);
    const std::vector<CustomGCode::Item>& color_print_values = wxGetApp().is_editor() ?
        wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes : m_gcode_result->custom_gcode_per_print_z;
    std::vector<std::string> color_print_colors;
    if (!color_print_values.empty()) {
        color_print_colors = wxGetApp().plater()->get_colors_for_color_print(m_gcode_result);
        color_print_colors.push_back("#808080"); // gray color for pause print or custom G-code 
    }

    std::vector<double> zs;

    if (IsShown()) {
        m_canvas->set_selected_extruder(0);
        if (gcode_preview_data_valid) {
            // Load the real G-code preview.
            m_canvas->load_gcode_preview(*m_gcode_result, tool_colors, color_print_colors);
            // the view type may have been changed by the call m_canvas->load_gcode_preview()
            gcode_view_type = m_canvas->get_gcode_view_type();
            m_left_sizer->Layout();
            Refresh();
            zs = m_canvas->get_gcode_layers_zs();
            if (!zs.empty())
                m_left_sizer->Show(m_bottom_toolbar_panel);
            m_loaded = true;
        }
        else if (is_pregcode_preview) {
            // Load the initial preview based on slices, not the final G-code.
            m_canvas->load_preview(tool_colors, color_print_colors, color_print_values);
            // the view type has been changed by the call m_canvas->load_gcode_preview()
            if (gcode_view_type == libvgcode::EViewType::ColorPrint && !color_print_values.empty())
                m_canvas->set_gcode_view_type(gcode_view_type);
            m_left_sizer->Hide(m_bottom_toolbar_panel);
            m_left_sizer->Layout();
            Refresh();
            zs = m_canvas->get_gcode_layers_zs();
        }
        else {
            m_left_sizer->Hide(m_bottom_toolbar_panel);
            m_left_sizer->Layout();
            Refresh();
        }

        if (!zs.empty() && !m_keep_current_preview_type) {
            const unsigned int number_extruders = wxGetApp().is_editor() ?
                (unsigned int)print->extruders().size() : m_canvas->get_gcode_extruders_count();
            const bool contains_color_gcodes = std::any_of(std::begin(color_print_values), std::end(color_print_values),
                [](auto const& item) { return item.type == CustomGCode::Type::ColorChange || item.type == CustomGCode::Type::ToolChange; });
            const libvgcode::EViewType choice = contains_color_gcodes ?
                libvgcode::EViewType::ColorPrint :
                (number_extruders > 1) ? libvgcode::EViewType::Tool : libvgcode::EViewType::FeatureType;
            if (choice != gcode_view_type) {
                const bool gcode_view_type_cache_load = m_canvas->is_gcode_view_type_cache_load_enabled();
                if (gcode_view_type_cache_load)
                    m_canvas->enable_gcode_view_type_cache_load(false);
                m_canvas->set_gcode_view_type(choice);
                if (gcode_view_type_cache_load)
                    m_canvas->enable_gcode_view_type_cache_load(true);
                if (wxGetApp().is_gcode_viewer())
                    m_keep_current_preview_type = true;
            }
        }

        if (zs.empty()) {
            // all layers filtered out
            hide_layers_slider();
            m_canvas_widget->Refresh();
        }
        else
            update_layers_slider(zs, keep_z_range);
    }
}

void Preview::load_print_as_sla()
{
    if (m_loaded || (m_process->current_printer_technology() != ptSLA))
        return;

    unsigned int n_layers = 0;
    const SLAPrint* print = m_process->sla_print();

    std::vector<double> zs;
    double initial_layer_height = print->material_config().initial_layer_height.value;
    for (const SLAPrintObject* obj : print->objects())
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty()) {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (auto& rec : obj->get_slice_index())
                zs.emplace_back(initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    sort_remove_duplicates(zs);

    m_canvas->reset_clipping_planes_cache();
    m_canvas->set_use_clipping_planes(true);

    n_layers = (unsigned int)zs.size();
    if (n_layers == 0) {
        hide_layers_slider();
        m_canvas_widget->Refresh();
    }

    if (IsShown()) {
        m_canvas->load_sla_preview();
        m_left_sizer->Hide(m_bottom_toolbar_panel);
        m_left_sizer->Layout();
        Refresh();

        if (n_layers > 0)
            update_layers_slider(zs);

        m_loaded = true;
    }
}

void Preview::on_layers_slider_scroll_changed(wxCommandEvent& event)
{
    if (IsShown()) {
        PrinterTechnology tech = m_process->current_printer_technology();
        if (tech == ptFFF) {
            m_canvas->set_volumes_z_range({ m_layers_slider->GetLowerValueD(), m_layers_slider->GetHigherValueD() });
            m_canvas->set_toolpaths_z_range({ static_cast<unsigned int>(m_layers_slider->GetLowerValue()), static_cast<unsigned int>(m_layers_slider->GetHigherValue()) });
            m_canvas->set_as_dirty();
        }
        else if (tech == ptSLA) {
            m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -m_layers_slider->GetLowerValueD()));
            m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), m_layers_slider->GetHigherValueD()));
            m_canvas->render();
        }
    }
}

void Preview::on_moves_slider_scroll_changed(wxCommandEvent& event)
{
    m_canvas->update_gcode_sequential_view_current(static_cast<unsigned int>(m_moves_slider->GetLowerValueD() - 1.0), static_cast<unsigned int>(m_moves_slider->GetHigherValueD() - 1.0));
    m_canvas->set_as_dirty();
    m_canvas->request_extra_frame();
}

} // namespace GUI
} // namespace Slic3r
