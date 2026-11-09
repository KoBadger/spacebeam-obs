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
#include <QLineEdit>
#include <QString>
#include <QVector>
#include <QSignalBlocker>
#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QRect>
#include <functional>


/* The main plugin resolves these with GetProcAddress, so they must be exported
 * from this DLL. */
#ifdef SB_DOCK_DLL
#define SB_DOCK_API extern "C" __declspec(dllexport)
#else
#define SB_DOCK_API extern "C"
#endif

SB_DOCK_API bool sb_dock_register(void);
SB_DOCK_API void sb_dock_unregister(void);

#define SB_FILTER_ID "spacebeam_filter"
#define SB_DOCK_ID "spacebeam_dock"

namespace {

constexpr int kSlots = 9;

// Which slot a pad shows, whether it holds a saved look, and its name.
struct SlotState {
	bool filled = false;
	QString name;
	int slot = 0;
};

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
		root->addWidget(new QLabel(tr("Presets (click to load, right-click to clear)"), this));
		auto *grid = new QGridLayout();
		grid->setSpacing(4);
		for (int i = 0; i < kSlots; i++) {
			auto *pad = new QPushButton(QString::number(i + 1), this);
			pad->setMinimumHeight(34);
			pad->setContextMenuPolicy(Qt::CustomContextMenu);
			grid->addWidget(pad, i / 3, i % 3);
			pads_[i] = pad;

			connect(pad, &QPushButton::clicked, this, [this, i]() { loadPreset(i + 1); });
			connect(pad, &QWidget::customContextMenuRequested, this,
				[this, i](const QPoint &) { clearPreset(i + 1); });
		}
		root->addLayout(grid);

		/* ---- save row ---- */
		auto *saveRow = new QHBoxLayout();
		saveRow->addWidget(new QLabel(tr("Save to"), this));
		saveSlot_ = new QSpinBox(this);
		saveSlot_->setRange(1, kSlots);
		saveSlot_->setValue(1);
		saveRow->addWidget(saveSlot_);

		nameEdit_ = new QLineEdit(this);
		nameEdit_->setPlaceholderText(tr("name (optional)"));
		saveRow->addWidget(nameEdit_, 1);

		auto *saveBtn = new QPushButton(tr("Save"), this);
		saveRow->addWidget(saveBtn);
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
			[this](int) { onFilterChanged(); });
		connect(saveBtn, &QPushButton::clicked, this,
			[this]() { savePreset(saveSlot_->value()); });

		connect(transition_, &QSlider::valueChanged, this, [this](int v) {
			transVal_->setText(QString::number(v / 100.0, 'f', 2) + tr(" s"));
			pushSettings();
		});
		connect(bpm_, &QSlider::valueChanged, this, [this](int v) {
			bpmVal_->setText(QString::number(v) + tr(" BPM"));
			pushSettings();
		});

		refreshFilters();
		slotSig_ = slotSignature();
		startPolling();
	}

	~SpaceBeamDock() override { clearFilters(); }

private:
	QComboBox *combo_ = nullptr;
	QPushButton *pads_[kSlots] = {nullptr};
	QSpinBox *saveSlot_ = nullptr;
	QLineEdit *nameEdit_ = nullptr;
	QSlider *transition_ = nullptr;
	QLabel *transVal_ = nullptr;
	QSlider *bpm_ = nullptr;
	QLabel *bpmVal_ = nullptr;
	QLabel *status_ = nullptr;
	QTimer *poll_ = nullptr;

	QVector<FilterEntry> entries_;
	unsigned long slotSig_ = 0;

	/* ---- target handling ------------------------------------------- */

	obs_source_t *current()
	{
		const int idx = combo_ ? combo_->currentIndex() : -1;
		if (idx < 0 || idx >= entries_.size())
			return nullptr;

		obs_source_t *f = entries_[idx].filter;

		/* A filter can be deleted while the dock holds a reference. Detect
		 * that instead of writing settings into something detached. */
		if (!f || obs_source_removed(f)) {
			QMetaObject::invokeMethod(this, [this]() { refreshFilters(); },
						  Qt::QueuedConnection);
			return nullptr;
		}
		return f;
	}

	bool require(std::function<void(obs_source_t *)> fn)
	{
		obs_source_t *f = current();
		if (!f) {
			status_->setText(tr("That filter is gone. Refreshing…"));
			return false;
		}
		fn(f);
		return true;
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
		if (obs_source_removed(child))
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
		const QString keep = combo_ ? combo_->currentText() : QString();

		clearFilters();
		if (combo_)
			combo_->clear();

		ScanCtx ctx{&entries_};
		obs_enum_sources(&SpaceBeamDock::enum_source_cb, &ctx);

		if (entries_.isEmpty()) {
			status_->setText(tr("No SpaceBeam filter found. Add one to a source first."));
			setPadsEnabled(false);
			refreshPads();
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
		if (combo_)
			combo_->setCurrentIndex(restore >= 0 ? restore : 0);

		setPadsEnabled(true);
		syncFromFilter();
	}

	void onFilterChanged()
	{
		if (!current()) {
			refreshPads();
			return;
		}
		syncFromFilter();
	}

	void setPadsEnabled(bool on)
	{
		for (int i = 0; i < kSlots; i++) {
			if (pads_[i])
				pads_[i]->setEnabled(on);
		}
	}

	/* ---- slot state ------------------------------------------------- */

	/* Cheap fingerprint of the stored slots. Presets can also be changed from
	 * the properties dialog, a hotkey or a scene-collection reload, none of
	 * which the dock hears about, so poll this and refresh only on change. */
	unsigned long slotSignature()
	{
		unsigned long sig = 1469598103UL;
		obs_source_t *f = current();
		if (!f)
			return 0;

		obs_data_t *st = obs_source_get_settings(f);
		if (!st)
			return 0;

		for (int i = 0; i < kSlots; i++) {
			char key[32];
			snprintf(key, sizeof(key), "preset_%d", i + 1);
			const char *json = obs_data_get_string(st, key);
			const unsigned long len = json ? (unsigned long)strlen(json) : 0UL;
			sig = (sig ^ len) * 16777619UL;
		}

		obs_data_release(st);
		return sig;
	}

	void startPolling()
	{
		auto *poll = new QTimer(this);
		poll->setInterval(1000);
		connect(poll, &QTimer::timeout, this, [this]() {
			const unsigned long sig = slotSignature();
			if (sig && sig != slotSig_) {
				slotSig_ = sig;
				refreshPads();
			}
		});
		poll->start();
	}

	/* obs_source_update() hands the new settings to the filter, but the
	 * filter writes the resulting slot data back on its own thread, so an
	 * immediate read here still sees the old snapshot. Re-read shortly
	 * after instead of showing stale pads. */
	void refreshSoon()
	{
		QTimer::singleShot(150, this, [this]() {
			refreshPads();
			syncFromFilter();
		});
	}

	void refreshPads()
	{
		SlotState state[kSlots];
		int curSlot = -1;

		obs_source_t *f = current();
		if (f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (st) {
				curSlot = (int)obs_data_get_int(st, "preset_slot");

				for (int i = 0; i < kSlots; i++) {
					const QString key = QStringLiteral("preset_%1").arg(i + 1);
					const char *json = obs_data_get_string(st, key.toUtf8().constData());
					if (!json || !*json)
						continue;

					state[i].filled = true;
					/* The display name travels inside the snapshot
					 * JSON, so parse it back out for the label. */
					obs_data_t *snap = obs_data_create_from_json(json);
					if (snap) {
						const char *nm = obs_data_get_string(snap, "_name");
						if (nm && *nm)
							state[i].name = QString::fromUtf8(nm);
						obs_data_release(snap);
					}
				}
				obs_data_release(st);
			}
		}

		for (int i = 0; i < kSlots; i++) {
			QPushButton *pad = pads_[i];
			if (!pad)
				continue;

			QString label = QString::number(i + 1);
			if (!state[i].name.isEmpty())
				label = QStringLiteral("%1\n%2").arg(i + 1).arg(state[i].name);
			else if (state[i].filled)
				label = QStringLiteral("%1 •").arg(i + 1);

			pad->setText(label);
			pad->setToolTip(state[i].filled
						? tr("Load preset %1%2")
							  .arg(i + 1)
							  .arg(state[i].name.isEmpty()
								       ? QString()
								       : " (" + state[i].name + ")")
						: tr("Preset %1 is empty — save a look here first").arg(i + 1));

			/* Mark the slot the filter is currently pointed at. */
			const bool active = (curSlot == i + 1);
			pad->setStyleSheet(active ? QStringLiteral("font-weight:bold;")
						  : QStringLiteral(""));
		}

		if (f) {
			const int filled = [&]() {
				int n = 0;
				for (int i = 0; i < kSlots; i++)
					if (state[i].filled)
						n++;
				return n;
			}();
			status_->setText(tr("%1/%2 slots used").arg(filled).arg(kSlots));
		}

		slotSig_ = slotSignature();
	}

	/* ---- settings plumbing ------------------------------------------ */

	void syncFromFilter()
	{
		obs_source_t *f = current();
		if (!f) {
			refreshPads();
			return;
		}

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
		refreshPads();
	}

	void pushSettings()
	{
		require([this](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;
			obs_data_set_double(st, "preset_time",
					    transition_ ? transition_->value() / 100.0 : 1.0);
			obs_data_set_double(st, "bpm", bpm_ ? bpm_->value() : 120.0);
			obs_source_update(f, st);
			obs_data_release(st);
		});
	}

	void loadPreset(int slot)
	{
		require([this, slot](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;

			const QString key = QStringLiteral("preset_%1").arg(slot);
			const char *json = obs_data_get_string(st, key.toUtf8().constData());
			if (!json || !*json) {
				status_->setText(tr("Preset %1 is empty.").arg(slot));
				obs_data_release(st);
				return;
			}

			obs_data_set_int(st, "preset_slot", slot);
			obs_data_set_double(st, "preset_time",
					    transition_ ? transition_->value() / 100.0 : 1.0);
			obs_data_set_int(st, "preset_cmd", 2); /* load */
			obs_source_update(f, st);
			obs_data_release(st);

			status_->setText(tr("Loaded preset %1").arg(slot));
			refreshSoon();
		});
	}

	void savePreset(int slot)
	{
		require([this, slot](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;

			obs_data_set_int(st, "preset_slot", slot);
			obs_data_set_string(st, "preset_name",
					    nameEdit_ ? nameEdit_->text().toUtf8().constData() : "");
			obs_data_set_int(st, "preset_cmd", 1); /* save */
			obs_source_update(f, st);
			obs_data_release(st);

			status_->setText(tr("Saved to slot %1").arg(slot));
			refreshSoon();
		});
	}

	void clearPreset(int slot)
	{
		require([this, slot](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;

			obs_data_set_int(st, "preset_slot", slot);
			obs_data_set_int(st, "preset_cmd", 3); /* clear */
			obs_source_update(f, st);
			obs_data_release(st);

			status_->setText(tr("Cleared slot %1").arg(slot));
			refreshSoon();
		});
	}
};

SpaceBeamDock *g_dock = nullptr;
bool g_registered = false;

/* Docks added through the frontend API start hidden, which makes the panel
/* Docks added through the frontend API start hidden, which makes the panel
 * easy to miss. OBS does not persist a floating dock across restarts, so a
 * one-shot "already shown" flag would lose the panel forever after the first
 * launch. Instead restore it on every start: show it in the dock area, and if
 * OBS refuses to surface it there, fall back to floating. */
void reveal_dock(SpaceBeamDock *widget)
{
	auto *win = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!win)
		return;

	QTimer::singleShot(0, win, [win, widget]() {
		const auto docks = win->findChildren<QDockWidget *>();
		for (QDockWidget *dock : docks) {
			if (dock->widget() != widget)
				continue;

			if (!dock->isVisible()) {
				dock->show();
				/* A hidden dock can ignore show() because of the saved
				 * area state; floating always works. */
				if (!dock->isVisible()) {
					dock->setFloating(true);
					dock->resize(330, 480);
					const QRect frame = win->frameGeometry();
					dock->move(frame.right() - 370, frame.top() + 120);
					dock->show();
				}
			}
			dock->raise();
			return;
		}
		blog(LOG_WARNING, "[spacebeam] dock wrapper not found (%d docks)",
		     (int)docks.size());
	});
}

} // namespace

SB_DOCK_API bool sb_dock_register(void)
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
	reveal_dock(g_dock);
	return true;
}

SB_DOCK_API void sb_dock_unregister(void)
{
	if (!g_registered)
		return;

	obs_frontend_remove_dock(SB_DOCK_ID);
	g_registered = false;
	g_dock = nullptr; /* ownership passed to OBS */
}
