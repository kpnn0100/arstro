#include "Accessibility.h"
#include <atk/atk.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace arstro
{
namespace solaris_host
{
    /** What a control's ATK object says — kept beside it, so AT-SPI's calls answer from the last list. */
    struct NodeData
    {
        A11yItem item;
        double scale = 1.0;
        bool focused = false;
        AtkBridge::Impl *impl = nullptr; // null once the bridge is gone: its calls then do nothing
    };

    struct AtkBridge::Impl
    {
        AtkBridge *owner = nullptr;
        AtkObject *root = nullptr;
        std::vector<AtkObject *> kids; // in reading order, one reference each
        int ox = 0, oy = 0;
        std::string focused;
    };
}
}

using arstro::solaris_host::NodeData;
using BridgeImpl = arstro::solaris_host::AtkBridge::Impl;

extern "C" {
typedef struct
{
    AtkObject parent;
    NodeData *data;
} SolarisA11yNode;
typedef struct
{
    AtkObjectClass parent_class;
} SolarisA11yNodeClass;
typedef struct
{
    SolarisA11yNode parent;
} SolarisA11yValueNode;
typedef struct
{
    SolarisA11yNodeClass parent_class;
} SolarisA11yValueNodeClass;
typedef struct
{
    AtkObject parent;
    BridgeImpl *impl;
} SolarisA11yRoot;
typedef struct
{
    AtkObjectClass parent_class;
} SolarisA11yRootClass;
}

namespace
{
    NodeData *dataOf(gpointer o) { return o ? ((SolarisA11yNode *)o)->data : nullptr; }

    AtkRole roleOf(const std::string &r)
    {
        if (r == "button") return ATK_ROLE_PUSH_BUTTON;
        if (r == "toggle") return ATK_ROLE_TOGGLE_BUTTON;
        if (r == "slider") return ATK_ROLE_SLIDER;
        if (r == "combo") return ATK_ROLE_COMBO_BOX;
        if (r == "tab") return ATK_ROLE_PAGE_TAB;
        if (r == "menu") return ATK_ROLE_MENU;
        if (r == "menuitem") return ATK_ROLE_MENU_ITEM;
        if (r == "listitem") return ATK_ROLE_LIST_ITEM;
        return ATK_ROLE_UNKNOWN;
    }

    /** A control with a value to read and step — its object is the kind that answers AtkValue. */
    bool valued(const arstro::solaris_host::A11yItem &it) { return it.steps || it.role == "slider"; }
}

// ── a control ───────────────────────────────────────────────────────────────────────────────────────────────────────

static void solaris_a11y_node_component_init(AtkComponentIface *iface);
static void solaris_a11y_node_action_init(AtkActionIface *iface);

G_DEFINE_TYPE_WITH_CODE(SolarisA11yNode, solaris_a11y_node, ATK_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, solaris_a11y_node_component_init)
                        G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, solaris_a11y_node_action_init))

static AtkStateSet *node_ref_state_set(AtkObject *o)
{
    AtkStateSet *s = ATK_OBJECT_CLASS(solaris_a11y_node_parent_class)->ref_state_set(o);
    const NodeData *d = dataOf(o);
    if (!d || !d->impl)
    {
        atk_state_set_add_state(s, ATK_STATE_DEFUNCT);
        return s;
    }
    atk_state_set_add_state(s, ATK_STATE_VISIBLE);
    atk_state_set_add_state(s, ATK_STATE_SHOWING);
    atk_state_set_add_state(s, ATK_STATE_FOCUSABLE);
    if (d->item.enabled)
    {
        atk_state_set_add_state(s, ATK_STATE_ENABLED);
        atk_state_set_add_state(s, ATK_STATE_SENSITIVE);
    }
    if (d->focused) atk_state_set_add_state(s, ATK_STATE_FOCUSED);
    if (d->item.role == "toggle") atk_state_set_add_state(s, ATK_STATE_CHECKABLE);
    if (d->item.checked) atk_state_set_add_state(s, d->item.role == "tab" ? ATK_STATE_SELECTED : ATK_STATE_CHECKED);
    return s;
}

static gint node_get_index_in_parent(AtkObject *o)
{
    const NodeData *d = dataOf(o);
    if (!d || !d->impl) return -1;
    const auto &k = d->impl->kids;
    const auto it = std::find(k.begin(), k.end(), o);
    return it == k.end() ? -1 : (gint)(it - k.begin());
}

static void node_finalize(GObject *o)
{
    delete dataOf(o);
    ((SolarisA11yNode *)o)->data = nullptr;
    G_OBJECT_CLASS(solaris_a11y_node_parent_class)->finalize(o);
}

static void solaris_a11y_node_class_init(SolarisA11yNodeClass *k)
{
    AtkObjectClass *a = ATK_OBJECT_CLASS(k);
    a->ref_state_set = node_ref_state_set;
    a->get_index_in_parent = node_get_index_in_parent;
    G_OBJECT_CLASS(k)->finalize = node_finalize;
}

static void solaris_a11y_node_init(SolarisA11yNode *n) { n->data = nullptr; }

// where it is: the window's pixels, or the screen's
static void node_get_extents(AtkComponent *c, gint *x, gint *y, gint *w, gint *h, AtkCoordType type)
{
    const NodeData *d = dataOf(c);
    if (!d)
    {
        *x = *y = *w = *h = 0;
        return;
    }
    const double s = d->scale;
    *x = (gint)std::lround(d->item.x * s);
    *y = (gint)std::lround(d->item.y * s);
    *w = (gint)std::lround(d->item.w * s);
    *h = (gint)std::lround(d->item.h * s);
    if (type == ATK_XY_SCREEN && d->impl)
    {
        *x += d->impl->ox;
        *y += d->impl->oy;
    }
}

static void solaris_a11y_node_component_init(AtkComponentIface *i) { i->get_extents = node_get_extents; }

// what it does: its click; a value's steps — back to the App, as the keyboard's
static gint node_n_actions(AtkAction *a)
{
    const NodeData *d = dataOf(a);
    return !d ? 0 : d->item.steps ? 3 : 1;
}

static const gchar *node_action_name(AtkAction *a, gint i)
{
    if (i < 0 || i >= node_n_actions(a)) return nullptr;
    return i == 0 ? "click" : i == 1 ? "increment" : "decrement";
}

static gboolean node_do_action(AtkAction *a, gint i)
{
    const NodeData *d = dataOf(a);
    if (!d || !d->impl || i < 0 || i >= node_n_actions(a)) return FALSE;
    arstro::solaris_host::AtkBridge *b = d->impl->owner;
    const std::string key = d->item.key; // the callback may change what the bridge holds
    if (i == 0) return b->onActivate && b->onActivate(key) ? TRUE : FALSE;
    return b->onStep && b->onStep(key, i == 1 ? 1 : -1) ? TRUE : FALSE;
}

static void solaris_a11y_node_action_init(AtkActionIface *i)
{
    i->get_n_actions = node_n_actions;
    i->get_name = node_action_name;
    i->do_action = node_do_action;
}

// ── a control with a value ──────────────────────────────────────────────────────────────────────────────────────────

static void solaris_a11y_value_node_value_init(AtkValueIface *iface);

G_DEFINE_TYPE_WITH_CODE(SolarisA11yValueNode, solaris_a11y_value_node, solaris_a11y_node_get_type(),
                        G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, solaris_a11y_value_node_value_init))

static void solaris_a11y_value_node_class_init(SolarisA11yValueNodeClass *) {}
static void solaris_a11y_value_node_init(SolarisA11yValueNode *) {}

// what it says: its words, and the number they begin with ("−4.5 dB" → −4.5: the minus the UI draws is U+2212)
static void node_value_and_text(AtkValue *v, gdouble *value, gchar **text)
{
    const NodeData *d = dataOf(v);
    const std::string s = d ? d->item.value : std::string();
    if (value)
    {
        static const std::string kMinus = "\xE2\x88\x92";
        const bool neg = s.compare(0, kMinus.size(), kMinus) == 0;
        const double x = std::strtod(s.c_str() + (neg ? kMinus.size() : 0), nullptr);
        *value = neg ? -x : x;
    }
    if (text) *text = g_strdup(s.c_str());
}

static void solaris_a11y_value_node_value_init(AtkValueIface *i) { i->get_value_and_text = node_value_and_text; }

// ── the window's content ────────────────────────────────────────────────────────────────────────────────────────────

G_DEFINE_TYPE(SolarisA11yRoot, solaris_a11y_root, ATK_TYPE_OBJECT)

static gint root_n_children(AtkObject *o)
{
    const BridgeImpl *impl = ((SolarisA11yRoot *)o)->impl;
    return impl ? (gint)impl->kids.size() : 0;
}

static AtkObject *root_ref_child(AtkObject *o, gint i)
{
    const BridgeImpl *impl = ((SolarisA11yRoot *)o)->impl;
    if (!impl || i < 0 || i >= (gint)impl->kids.size()) return nullptr;
    return (AtkObject *)g_object_ref(impl->kids[(size_t)i]);
}

static void solaris_a11y_root_class_init(SolarisA11yRootClass *k)
{
    AtkObjectClass *a = ATK_OBJECT_CLASS(k);
    a->get_n_children = root_n_children;
    a->ref_child = root_ref_child;
}

static void solaris_a11y_root_init(SolarisA11yRoot *r) { r->impl = nullptr; }

// ── the bridge ──────────────────────────────────────────────────────────────────────────────────────────────────────

namespace arstro
{
namespace solaris_host
{
    AtkBridge::AtkBridge() : d(new Impl())
    {
        d->owner = this;
        d->root = ATK_OBJECT(g_object_new(solaris_a11y_root_get_type(), nullptr));
        ((SolarisA11yRoot *)d->root)->impl = d.get();
        atk_object_set_role(d->root, ATK_ROLE_FILLER);
        atk_object_set_name(d->root, "Solaris");
    }

    AtkBridge::~AtkBridge()
    {
        for (AtkObject *k : d->kids)
        {
            if (NodeData *nd = dataOf(k)) nd->impl = nullptr; // a screen reader still holding one finds it defunct
            g_object_unref(k);
        }
        d->kids.clear();
        ((SolarisA11yRoot *)d->root)->impl = nullptr;
        g_object_unref(d->root);
    }

    AtkObject *AtkBridge::root() const { return d->root; }
    int AtkBridge::count() const { return (int)d->kids.size(); }

    AtkObject *AtkBridge::child(const std::string &key) const
    {
        for (AtkObject *k : d->kids)
            if (const NodeData *nd = dataOf(k); nd && nd->item.key == key) return k;
        return nullptr;
    }

    void AtkBridge::setOrigin(int x, int y)
    {
        d->ox = x;
        d->oy = y;
    }

    void AtkBridge::update(const std::vector<A11yItem> &items, double scale)
    {
        std::map<std::string, AtkObject *> old;
        for (AtkObject *k : d->kids) old[dataOf(k)->item.key] = k;
        std::vector<AtkObject *> next;
        std::vector<bool> fresh;
        for (const auto &it : items)
        {
            AtkObject *o = nullptr;
            bool made = false;
            if (const auto f = old.find(it.key); f != old.end() && valued(dataOf(f->second)->item) == valued(it))
            {
                o = f->second;
                old.erase(f);
            }
            else
            {
                o = ATK_OBJECT(g_object_new(valued(it) ? solaris_a11y_value_node_get_type() : solaris_a11y_node_get_type(), nullptr));
                auto *nd = new NodeData();
                nd->impl = d.get();
                ((SolarisA11yNode *)o)->data = nd;
                atk_object_set_parent(o, d->root);
                made = true;
            }
            NodeData *nd = dataOf(o);
            const A11yItem was = nd->item;
            nd->item = it;
            nd->scale = scale;
            nd->focused = !d->focused.empty() && it.key == d->focused;
            if (made || was.role != it.role) atk_object_set_role(o, roleOf(it.role));
            if (made || was.name != it.name) atk_object_set_name(o, it.name.c_str());
            if (made || was.value != it.value) atk_object_set_description(o, it.value.c_str()); // said as it changes
            if (!made && was.checked != it.checked)
                atk_object_notify_state_change(o, it.role == "tab" ? ATK_STATE_SELECTED : ATK_STATE_CHECKED, it.checked ? TRUE : FALSE);
            if (!made && was.enabled != it.enabled) atk_object_notify_state_change(o, ATK_STATE_ENABLED, it.enabled ? TRUE : FALSE);
            next.push_back(o);
            fresh.push_back(made);
        }
        // the ones gone: the new list in place first (a listener reads the root), each said where it was — the last
        // first, so the places still mean what they say — then let go
        std::vector<std::pair<gint, AtkObject *>> gone;
        for (const auto &kv : old)
            gone.push_back({(gint)(std::find(d->kids.begin(), d->kids.end(), kv.second) - d->kids.begin()), kv.second});
        std::sort(gone.begin(), gone.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        d->kids = next;
        for (const auto &g : gone)
        {
            dataOf(g.second)->impl = nullptr; // defunct from here
            g_signal_emit_by_name(d->root, "children-changed::remove", g.first, g.second);
        }
        for (const auto &g : gone) g_object_unref(g.second);
        for (size_t i = 0; i < next.size(); ++i)
            if (fresh[i])
            {
                g_signal_emit_by_name(d->root, "children-changed::add", (gint)i, next[i]);
                if (dataOf(next[i])->focused) // the focus moved onto it before it was listed: said now it is
                {
                    atk_object_notify_state_change(next[i], ATK_STATE_FOCUSED, TRUE);
                    g_signal_emit_by_name(next[i], "focus-event", TRUE);
                }
            }
    }

    void AtkBridge::focus(const std::string &key)
    {
        d->focused = key;
        for (AtkObject *k : d->kids)
        {
            NodeData *nd = dataOf(k);
            const bool f = !key.empty() && nd->item.key == key;
            if (f == nd->focused) continue;
            nd->focused = f;
            atk_object_notify_state_change(k, ATK_STATE_FOCUSED, f ? TRUE : FALSE);
            if (f) g_signal_emit_by_name(k, "focus-event", TRUE);
        }
    }
}
}
