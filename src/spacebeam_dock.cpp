// SpaceBeam dock panel.
//
// A Qt control surface for the SpaceBeam filter: pick a filter, fire preset
// pads, and drive transition time / tempo without opening the properties
// dialog. Kept deliberately small - no custom signals or Q_OBJECT, so no moc
// step is required.
//
// Built against the same Qt minor version OBS ships (6.11.1). If OBS is
// upgraded to a different Qt minor this file must be rebuilt against it.

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QVector>
#include <QSignalBlocker>
#include <QDockWidget>
#include <QMainWindow>
#include <QMetaObject>
#include <QTimer>

extern "C" {
bool sb_dock_register(void);
void sb_dock_unregister(void);
}

#define SB_FILTER_ID "spacebeam_filter"
#define SB_DOCK_ID "spacebeam_dock"

namespace {

struct FilterEntry {
	QString label;
	obs_source_t *filter; // held reference
};

class SpaceBeamDock : public QWidget {
public:
	explicit SpaceBeamDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(8, 8, 8, 8);
		root->setSpacing(6);

		/* ---- target filter ---- */
		auto *topRow = new QHBoxLayout();
		topRow->addWidget(new QLabel(tr("Filter:"), this));
		combo_ = new QComboBox(this);
		combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		topRow->addWidget(combo_, 1);
		auto *refresh = new QPushButton(tr("Refresh"), this);
		topRow->addWidget(refresh);
		root->addLayout(topRow);

		/* ---- preset pads ---- */
		root->addWidget(new QLabel(tr("Presets (click to load)"), this));
		auto *grid = new QGridLayout();
		grid->setSpacing(4);
		for (int i = 0; i < 9; i++) {
			auto *pad = new QPushButton(QString::number(i + 1), this);
			pad->setMinimumHeight(34);
			pad->setToolTip(tr("Load preset %1").arg(i + 1));
			grid->addWidget(pad, i / 3, i % 3);
			pads_[i] = pad;
			connect(pad, &QPushButton::clicked, this, [this, i]() { loadPreset(i + 1); });
		}
		root->addLayout(grid);

		/* ---- save row ---- */
		auto *saveRow = new QHBoxLayout();
		saveRow->addWidget(new QLabel(tr("Save current look to"), this));
		saveSlot_ = new QSpinBox(this);
		saveSlot_->setRange(1, 9);
		saveSlot_->setValue(1);
		saveRow->addWidget(saveSlot_);
		auto *saveBtn = new QPushButton(tr("Save"), this);
		saveRow->addWidget(saveBtn);
		saveRow->addStretch(1);
		root->addLayout(saveRow);

		/* ---- transition time ---- */
		auto *tRow = new QHBoxLayout();
		auto *tLbl = new QLabel(tr("Transition:"), this);
		tLbl->setMinimumWidth(70);
		tRow->addWidget(tLbl);
		transition_ = new QSlider(Qt::Horizontal, this);
		transition_->setRange(0, 2000); /* 0.00 .. 20.00 s */
		transition_->setValue(100);
		tRow->addWidget(transition_, 1);
		transVal_ = new QLabel(tr("1.00 s"), this);
		transVal_->setMinimumWidth(52);
		tRow->addWidget(transVal_);
		root->addLayout(tRow);

		/* ---- tempo ---- */
		auto *bRow = new QHBoxLayout();
		auto *bLbl = new QLabel(tr("Tempo:"), this);
		bLbl->setMinimumWidth(70);
		bRow->addWidget(bLbl);
		bpm_ = new QSlider(Qt::Horizontal, this);
		bpm_->setRange(20, 300);
		bpm_->setValue(120);
		bRow->addWidget(bpm_, 1);
		bpmVal_ = new QLabel(tr("120 BPM"), this);
		bpmVal_->setMinimumWidth(52);
		bRow->addWidget(bpmVal_);
		root->addLayout(bRow);

		status_ = new QLabel(tr("No SpaceBeam filter found."), this);
		status_->setWordWrap(true);
		root->addWidget(status_);
		root->addStretch(1);

		/* ---- wiring ---- */
		connect(refresh, &QPushButton::clicked, this, [this]() { refreshFilters(); });
		connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			[this](int) { syncFromFilter(); });
		connect(saveBtn, &QPushButton::clicked, this, [this]() { savePreset(saveSlot_->value()); });

		connect(transition_, &QSlider::valueChanged, this, [this](int v) {
			transVal_->setText(QString::number(v / 100.0, 'f', 2) + tr(" s"));
			pushSettings(false);
		});
		connect(bpm_, &QSlider::valueChanged, this, [this](int v) {
			bpmVal_->setText(QString::number(v) + tr(" BPM"));
			pushSettings(false);
		});

		refreshFilters();
	}

	~SpaceBeamDock() override { clearFilters(); }

private:
	QComboBox *combo_ = nullptr;
	QPushButton *pads_[9] = {nullptr};
	QSpinBox *saveSlot_ = nullptr;
	QSlider *transition_ = nullptr;
	QLabel *transVal_ = nullptr;
	QSlider *bpm_ = nullptr;
	QLabel *bpmVal_ = nullptr;
	QLabel *status_ = nullptr;

	QVector<FilterEntry> entries_;

	obs_source_t *current() const
	{
		int idx = combo_ ? combo_->currentIndex() : -1;
		if (idx < 0 || idx >= entries_.size())
			return nullptr;
		return entries_[idx].filter;
	}

	void clearFilters()
	{
		for (auto &e : entries_) {
			if (e.filter)
				obs_source_release(e.filter);
		}
		entries_.clear();
	}

	struct ScanCtx {
		QVector<FilterEntry> *out;
	};

	static void enum_filter_cb(obs_source_t *parent, obs_source_t *child, void *param)
	{
		auto *ctx = static_cast<ScanCtx *>(param);
		const char *id = obs_source_get_id(child);
		if (!id || strcmp(id, SB_FILTER_ID) != 0)
			return;

		const char *pname = obs_source_get_name(parent);
		const char *fname = obs_source_get_name(child);

		obs_source_t *ref = obs_source_get_ref(child);
		if (!ref)
			return;

		FilterEntry e;
		e.label = QStringLiteral("%1 / %2")
				  .arg(QString::fromUtf8(pname ? pname : "?"))
				  .arg(QString::fromUtf8(fname ? fname : "??"));
		e.filter = ref;
		ctx->out->push_back(e);
	}

	static bool enum_source_cb(void *param, obs_source_t *source)
	{
		auto *ctx = static_cast<ScanCtx *>(param);
		obs_source_enum_filters(source, &SpaceBeamDock::enum_filter_cb, ctx);
		return true;
	}

	void refreshFilters()
	{
		QString keep = combo_ ? combo_->currentText() : QString();

		clearFilters();
		if (combo_)
			combo_->clear();

		ScanCtx ctx{&entries_};
		obs_enum_sources(&SpaceBeamDock::enum_source_cb, &ctx);

		if (entries_.isEmpty()) {
			status_->setText(tr("No SpaceBeam filter found. Add one to a source first."));
			return;
		}

		for (const auto &e : entries_) {
			if (combo_)
				combo_->addItem(e.label);
		}

		int restore = -1;
		for (int i = 0; i < entries_.size(); i++) {
			if (entries_[i].label == keep) {
				restore = i;
				break;
			}
		}
		if (combo_) {
			combo_->setCurrentIndex(restore >= 0 ? restore : 0);
		}
		status_->setText(tr("%1 filter(s)").arg(entries_.size()));
		syncFromFilter();
	}

	void syncFromFilter()
	{
		obs_source_t *f = current();
		if (!f)
			return;

		obs_data_t *st = obs_source_get_settings(f);
		if (!st)
			return;

		double t = obs_data_get_double(st, "preset_time");
		if (t < 0.0)
			t = 0.0;
		if (t > 20.0)
			t = 20.0;

		double bpm = obs_data_get_double(st, "bpm");
		if (bpm < 20.0)
			bpm = 120.0;

		if (transition_) {
			QSignalBlocker b(transition_);
			transition_->setValue((int)(t * 100.0 + 0.5));
			transVal_->setText(QString::number(t, 'f', 2) + tr(" s"));
		}
		if (bpm_) {
			QSignalBlocker b(bpm_);
			bpm_->setValue((int)bpm);
			bpmVal_->setText(QString::number((int)bpm) + tr(" BPM"));
		}

		obs_data_release(st);
	}

	/* Write the dock's shared controls back into the filter. */
	void pushSettings(bool updateFromFiltered)
	{
		obs_source_t *f = current();
		if (!f)
			return;

		obs_data_t *st = obs_source_get_settings(f);
		if (!st)
			return;

		obs_data_set_double(st, "preset_time", transition_ ? transition_->value() / 100.0 : 1.0);
		obs_data_set_double(st, "bpm", bpm_ ? bpm_->value() : 120.0);

		obs_source_update(f, st);
		obs_data_release(st);
		(void)updateFromFiltered;
	}

	void loadPreset(int slot)
	{
		obs_source_t *f = current();
		if (!f) {
			status_->setText(tr("No filter selected."));
			return;
		}

		obs_data_t *st = obs_source_get_settings(f);
		if (!st)
			return;

		obs_data_set_int(st, "preset_slot", slot);
		obs_data_set_double(st, "preset_time", transition_ ? transition_->value() / 100.0 : 1.0);
		obs_data_set_int(st, "preset_cmd", 2); /* load */

		obs_source_update(f, st);
		obs_data_release(st);

		status_->setText(tr("Loaded preset %1").arg(slot));
	}

	void savePreset(int slot)
	{
		obs_source_t *f = current();
		if (!f) {
			status_->setText(tr("No filter selected."));
			return;
		}

		obs_data_t *st = obs_source_get_settings(f);
		if (!st)
			return;

		obs_data_set_int(st, "preset_slot", slot);
		obs_data_set_int(st, "preset_cmd", 1); /* save */

		obs_source_update(f, st);
		obs_data_release(st);

		status_->setText(tr("Saved to slot %1").arg(slot));
	}
};

SpaceBeamDock *g_dock = nullptr;
bool g_registered = false;

/* Docks added through the frontend API start hidden, which makes the panel
 * easy to miss. Reveal it once per profile; after that we respect whatever
 * the user's saved layout says. */
void reveal_once(SpaceBeamDock *widget)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return;

	const bool shown_before = config_get_bool(cfg, "SpaceBeam", "DockShown");
	if (shown_before)
		return;

	config_set_bool(cfg, "SpaceBeam", "DockShown", true);
	config_save(cfg);

	auto *win = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!win)
		return;

	/* The dock widget is created by the frontend during add_dock_by_id, so
	 * look it up by the id we registered. Defer a beat so layout settles. */
	QTimer::singleShot(0, win, [win]() {
		if (auto *dock = win->findChild<QDockWidget *>(SB_DOCK_ID))
			dock->show();
	});
	(void)widget;
}

} // namespace

extern "C" bool sb_dock_register(void)
{
	if (g_registered)
		return true;

	g_dock = new SpaceBeamDock(nullptr);
	if (!g_dock)
		return false;

	if (!obs_frontend_add_dock_by_id(SB_DOCK_ID, "SpaceBeam", g_dock)) {
		delete g_dock;
		g_dock = nullptr;
		blog(LOG_WARNING, "[spacebeam] could not register dock");
		return false;
	}

	g_registered = true;
	blog(LOG_INFO, "[spacebeam] dock registered");
	reveal_once(g_dock);
	return true;
}

extern "C" void sb_dock_unregister(void)
{
	if (!g_registered)
		return;

	obs_frontend_remove_dock(SB_DOCK_ID);
	g_registered = false;
	g_dock = nullptr; /* ownership passed to OBS */
}
