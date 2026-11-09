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
#include <string>

#include "sb_mod_targets.h"


/* The main plugin resolves these with GetProcAddress, so they must be exported
 * from this DLL. */
#ifdef SB_DOCK_DLL
#define SB_DOCK_API extern "C" __declspec(dllexport)
#else
#define SB_DOCK_API extern "C"
#endif

SB_DOCK_API bool sb_dock_register(void);
SB_DOCK_API void sb_dock_unregister(void);

/* Checked state needs to be obvious: the dock lives on OBS's dark theme. */
static const char *kLiveButtonStyle =
	"QPushButton { padding: 4px 10px; }"
	"QPushButton:checked { background-color: #2f6fd0; color: white;"
	" border: 1px solid #5b9bf0; }";

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
		/* Master on/off for the whole effect. A full-width bar at the very top:
		 * it is the control you reach for in a hurry, and a small chip in the
		 * filter row was too easy to miss. It drives the filter's enabled flag
		 * - the same switch as the checkbox in the Filters dialog - so one
		 * click bypasses every stage. */
		powerBtn_ = new QPushButton(this);
		powerBtn_->setCheckable(true);
		powerBtn_->setChecked(true);
		powerBtn_->setMinimumHeight(34);
		powerBtn_->setToolTip(tr("Turn the whole SpaceBeam effect on or off"));
		root->addWidget(powerBtn_);
		stylePowerButton(true);

		/* ---- target filter ---- */
		auto *topRow = new QHBoxLayout();
		topRow->addWidget(new QLabel(tr("Filter:"), this));
		combo_ = new QComboBox(this);
		combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		topRow->addWidget(combo_, 1);
		auto *refresh = new QPushButton(tr("Refresh"), this);
		topRow->addWidget(refresh);
		root->addLayout(topRow);

		/* ---- live controls -------------------------------------------
		 * These are the ones reached for mid-performance, so they sit
		 * above the presets rather than buried in the properties dialog. */

		/* kaleidoscope: instant on/off with an eased unfold, plus strength */
		auto *kRow = new QHBoxLayout();
		kaleidoBtn_ = new QPushButton(tr("Kaleidoscope"), this);
		kaleidoBtn_->setCheckable(true);
		kaleidoBtn_->setChecked(false);
		kaleidoBtn_->setMinimumHeight(28);
		kaleidoBtn_->setToolTip(
			tr("Toggle the kaleidoscope stage. It eases out and back in "
			   "using the morph time in the filter properties."));
		kaleidoBtn_->setStyleSheet(kLiveButtonStyle);
		kRow->addWidget(kaleidoBtn_);
		kRow->addSpacing(8);
		kaleido_ = new QSlider(Qt::Horizontal, this);
		kaleido_->setRange(0, 1000); /* amount 0..1, scaled for the slider */
		kaleido_->setValue(1000);
		kaleido_->setToolTip(tr("Kaleidoscope strength"));
		kRow->addWidget(kaleido_, 1);
		kaleidoVal_ = new QLabel(tr("100%"), this);
		kaleidoVal_->setMinimumWidth(42);
		kRow->addWidget(kaleidoVal_);
		root->addLayout(kRow);

		/* music: drive parameters from an audio source */
		auto *mRow = new QHBoxLayout();
		musicBtn_ = new QPushButton(tr("Music"), this);
		musicBtn_->setCheckable(true);
		musicBtn_->setMinimumHeight(28);
		musicBtn_->setToolTip(
			tr("React to an audio source. Pick the source and set how hard "
			   "it pushes; the routings live in the filter properties."));
		musicBtn_->setStyleSheet(kLiveButtonStyle);
		mRow->addWidget(musicBtn_);
		mRow->addSpacing(8);
		sens_ = new QSlider(Qt::Horizontal, this);
		sens_->setRange(1, 100); /* sensitivity 0.1 .. 10 */
		sens_->setValue(10);
		sens_->setToolTip(tr("Music sensitivity"));
		mRow->addWidget(sens_, 1);
		sensVal_ = new QLabel(tr("1.0x"), this);
		sensVal_->setMinimumWidth(42);
		mRow->addWidget(sensVal_);
		root->addLayout(mRow);

		auto *srcRow = new QHBoxLayout();
		auto *srcLbl = new QLabel(tr("From:"), this);
		srcLbl->setMinimumWidth(42);
		srcRow->addWidget(srcLbl);
		audioSrc_ = new QComboBox(this);
		audioSrc_->setToolTip(tr("Audio source to follow"));
		srcRow->addWidget(audioSrc_, 1);
		root->addLayout(srcRow);

		/* master: overall zoom and spin */
		auto *zRow = new QHBoxLayout();
		auto *zLbl = new QLabel(tr("Master zoom:"), this);
		zLbl->setMinimumWidth(84);
		zRow->addWidget(zLbl);
		mzoom_ = new QSlider(Qt::Horizontal, this);
		mzoom_->setRange(5, 500); /* mt_zoom 0.05 .. 5.00 */
		mzoom_->setValue(100);
		zRow->addWidget(mzoom_, 1);
		mzoomVal_ = new QLabel(tr("1.00"), this);
		mzoomVal_->setMinimumWidth(42);
		zRow->addWidget(mzoomVal_);
		root->addLayout(zRow);

		auto *sRow = new QHBoxLayout();
		auto *sLbl = new QLabel(tr("Master spin:"), this);
		sLbl->setMinimumWidth(84);
		sRow->addWidget(sLbl);
		mspin_ = new QSlider(Qt::Horizontal, this);
		mspin_->setRange(-200, 200); /* mt_spin -2 .. 2 */
		mspin_->setValue(0);
		sRow->addWidget(mspin_, 1);
		mspinVal_ = new QLabel(tr("0.00"), this);
		mspinVal_->setMinimumWidth(42);
		sRow->addWidget(mspinVal_);
		root->addLayout(sRow);

		/* ---- stage strengths ------------------------------------------- */
		root->addWidget(new QLabel(tr("Stages"), this));
		for (int i = 0; i < kStageCount; i++) {
			auto *row = new QHBoxLayout();
			auto *lbl = new QLabel(tr(stageName(i)), this);
			lbl->setMinimumWidth(84);
			row->addWidget(lbl);

			stage_[i] = new QSlider(Qt::Horizontal, this);
			stage_[i]->setRange(0, 1000); /* 0.00 .. 1.00 */
			row->addWidget(stage_[i], 1);

			stageVal_[i] = new QLabel(tr("0"), this);
			stageVal_[i]->setMinimumWidth(34);
			row->addWidget(stageVal_[i]);

			connect(stage_[i], &QSlider::valueChanged, this, [this, i](int v) {
				stageVal_[i]->setText(QString::number(v / 10) + tr("%"));
				writeDouble(kStageKeys[i], v / 1000.0);
				/* Feedback needs its own switch; driving the amount is
				 * enough for the others. */
				if (i == kStageFeedback)
					writeBool("fb_on", v > 10);
			});
			root->addLayout(row);
		}

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

		connect(powerBtn_, &QPushButton::toggled, this, [this](bool on) {
			if (loading_)
				return;
			require([this, on](obs_source_t *f) {
				obs_source_set_enabled(f, on);
				stylePowerButton(on);
				status_->setText(on ? tr("Effect on")
						    : tr("Effect bypassed"));
			});
		});

		connect(kaleidoBtn_, &QPushButton::toggled, this, [this](bool on) {
			if (loading_)
				return;
			require([this, on](obs_source_t *f) {
				obs_data_t *st = obs_source_get_settings(f);
				if (!st)
					return;
				obs_data_set_bool(st, "kaleido_on", on);
				obs_source_update(f, st);
				obs_data_release(st);
				status_->setText(on ? tr("Kaleidoscope on")
						    : tr("Kaleidoscope off"));
			});
		});
		connect(kaleido_, &QSlider::valueChanged, this, [this](int v) {
			kaleidoVal_->setText(QString::number(v / 10) + tr("%"));
			if (loading_)
				return;
			require([this, v](obs_source_t *f) {
				obs_data_t *st = obs_source_get_settings(f);
				if (!st)
					return;
				obs_data_set_double(st, "k_amt", v / 1000.0);
				obs_source_update(f, st);
				obs_data_release(st);
			});
		});

		connect(transition_, &QSlider::valueChanged, this, [this](int v) {
			transVal_->setText(QString::number(v / 100.0, 'f', 2) + tr(" s"));
			pushSettings();
		});
		connect(bpm_, &QSlider::valueChanged, this, [this](int v) {
			bpmVal_->setText(QString::number(v) + tr(" BPM"));
			pushSettings();
		});

		/* ---- music ---- */
		connect(musicBtn_, &QPushButton::toggled, this, [this](bool on) {
			if (loading_)
				return;

			/* Turning music on with no source selected would do nothing, so
			 * fall back to the first real audio source. */
			if (on) {
				const QString cur = audioSrc_->currentData().toString();
				if (cur.isEmpty() && audioSrc_->count() > 1) {
					QSignalBlocker b(audioSrc_);
					audioSrc_->setCurrentIndex(1);
					writeString("audio_src", audioSrc_->itemData(1).toString().toUtf8().constData());
				}
				writeAudioDefaults();
			}

			writeBool("audio_on", on);
			status_->setText(on ? tr("Music reaction on")
					    : tr("Music reaction off"));
		});
		connect(sens_, &QSlider::valueChanged, this, [this](int v) {
			sensVal_->setText(QString::number(v / 10.0, 'f', 1) + tr("x"));
			writeDouble("audio_sens", v / 10.0);
		});
		connect(audioSrc_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			[this](int idx) {
				if (idx < 0)
					return;
				const QString name = audioSrc_->itemData(idx).toString();
				writeString("audio_src", name.toUtf8().constData());
			});

		/* ---- master ---- */
		connect(mzoom_, &QSlider::valueChanged, this, [this](int v) {
			mzoomVal_->setText(QString::number(v / 100.0, 'f', 2));
			writeDouble("mt_zoom", v / 100.0);
		});
		connect(mspin_, &QSlider::valueChanged, this, [this](int v) {
			mspinVal_->setText(QString::number(v / 100.0, 'f', 2));
			writeDouble("mt_spin", v / 100.0);
		});

		refreshFilters();
		refreshAudioSources();
		slotSig_ = slotSignature();
		loading_ = false;

		/* One more sync now that the controls are fully built: the first pass
		 * ran before the filter list existed, so toggles would otherwise keep
		 * their constructed defaults. */
		syncFromFilter();
		if (musicBtn_ && musicBtn_->isChecked())
			writeAudioDefaults();

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
	QPushButton *powerBtn_ = nullptr;
	QPushButton *kaleidoBtn_ = nullptr;
	QSlider *kaleido_ = nullptr;
	QLabel *kaleidoVal_ = nullptr;
	QPushButton *musicBtn_ = nullptr;
	QSlider *sens_ = nullptr;
	QLabel *sensVal_ = nullptr;
	QComboBox *audioSrc_ = nullptr;
	QSlider *mzoom_ = nullptr;
	QLabel *mzoomVal_ = nullptr;
	QSlider *mspin_ = nullptr;
	QLabel *mspinVal_ = nullptr;

	enum { kStageDroste, kStageTunnel, kStageSwirl, kStageFeedback, kStageCount };
	static constexpr const char *kStageKeys[4] = {"d_mix", "tn_mix", "sw_str", "fb_alpha"};
	static const char *stageName(int i)
	{
		switch (i) {
		case kStageDroste: return "Droste";
		case kStageTunnel: return "Tunnel";
		case kStageSwirl: return "Swirl";
		default: return "Feedback";
		}
	}
	QSlider *stage_[kStageCount] = {};
	QLabel *stageVal_[kStageCount] = {};
	QLabel *status_ = nullptr;
	QTimer *poll_ = nullptr;

	QVector<FilterEntry> entries_;
	unsigned long slotSig_ = 0;

	/* True while the constructor fills the controls from the filter. Widget
	 * values change during that sync and must not be treated as user edits —
	 * otherwise merely loading a scene rewrites the filter's settings (which
	 * is how the music toggle used to switch itself on at startup). */
	bool loading_ = true;

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

	/* ---- audio source list ------------------------------------------ */

	struct AudioScan {
		QComboBox *combo;
		QString keep;
	};

	static bool enum_audio_cb(void *param, obs_source_t *source)
	{
		auto *ctx = static_cast<AudioScan *>(param);
		const uint32_t flags = obs_source_get_output_flags(source);
		if (!(flags & OBS_SOURCE_AUDIO))
			return true;

		const char *name = obs_source_get_name(source);
		if (name && *name)
			ctx->combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
		return true;
	}

	void refreshAudioSources()
	{
		if (!audioSrc_)
			return;

		/* Keep whatever the filter is configured with, so rebuilding the list
		 * (Refresh) does not silently change the source. */
		QString keep;
		obs_source_t *f = current();
		if (f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (st) {
				const char *cur = obs_data_get_string(st, "audio_src");
				if (cur && *cur)
					keep = QString::fromUtf8(cur);
				obs_data_release(st);
			}
		}

		QSignalBlocker block(audioSrc_);
		audioSrc_->clear();
		audioSrc_->addItem(tr("(nothing)"), QString()); /* explicit "off" */

		AudioScan ctx{audioSrc_, keep};
		obs_enum_sources(&SpaceBeamDock::enum_audio_cb, &ctx);

		int idx = audioSrc_->findData(keep);
		audioSrc_->setCurrentIndex(idx >= 0 ? idx : 0);

		blog(LOG_INFO, "[spacebeam] dock: %d audio source(s) listed",
		     (int)audioSrc_->count() - 1);
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

		double kamt = qBound(0.0, obs_data_get_double(st, "k_amt"), 1.0);
		if (kaleido_) {
			QSignalBlocker b(kaleido_);
			const int v = (int)(kamt * 1000.0 + 0.5);
			kaleido_->setValue(v);
			kaleidoVal_->setText(QString::number(v / 10) + tr("%"));
		}
		if (kaleidoBtn_) {
			QSignalBlocker b(kaleidoBtn_);
			kaleidoBtn_->setChecked(obs_data_get_bool(st, "kaleido_on"));
		}

		if (powerBtn_) {
			const bool en = obs_source_enabled(f);
			QSignalBlocker b(powerBtn_);
			powerBtn_->setChecked(en);
			stylePowerButton(en);
		}

		/* music */
		{
			QSignalBlocker b1(musicBtn_);
			musicBtn_->setChecked(obs_data_get_bool(st, "audio_on"));
			double sens = obs_data_get_double(st, "audio_sens");
			if (sens < 0.1)
				sens = 1.0;
			QSignalBlocker b2(sens_);
			const int sv = (int)(sens * 10.0 + 0.5);
			sens_->setValue(sv);
			sensVal_->setText(QString::number(sv / 10.0, 'f', 1) + tr("x"));

			const char *src = obs_data_get_string(st, "audio_src");
			QSignalBlocker b3(audioSrc_);
			const int idx = audioSrc_->findData(QString::fromUtf8(src ? src : ""));
			audioSrc_->setCurrentIndex(idx >= 0 ? idx : 0);
		}

		/* master */
		{
			double z = obs_data_get_double(st, "mt_zoom");
			if (z < 0.05)
				z = 1.0;
			QSignalBlocker b1(mzoom_);
			const int zv = (int)(z * 100.0 + 0.5);
			mzoom_->setValue(zv);
			mzoomVal_->setText(QString::number(zv / 100.0, 'f', 2));

			double sp = obs_data_get_double(st, "mt_spin");
			QSignalBlocker b2(mspin_);
			const int spv = (int)(sp * 100.0 + (sp < 0 ? -0.5 : 0.5));
			mspin_->setValue(spv);
			mspinVal_->setText(QString::number(spv / 100.0, 'f', 2));
		}

		/* stage strengths */
		for (int i = 0; i < kStageCount; i++) {
			if (!stage_[i])
				continue;
			double v = obs_data_get_double(st, kStageKeys[i]);
			if (v < 0.0)
				v = 0.0;
			if (v > 1.0)
				v = 1.0;
			QSignalBlocker b(stage_[i]);
			const int sv = (int)(v * 1000.0 + 0.5);
			stage_[i]->setValue(sv);
			stageVal_[i]->setText(QString::number(sv / 10) + tr("%"));
		}

		obs_data_release(st);
		refreshPads();
	}

	void pushSettings()
	{
		if (loading_)
			return;
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

	/* Small helpers so each control does not repeat the get/update dance. */
	void writeDouble(const char *key, double v)
	{
		if (loading_)
			return;
		require([key, v](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;
			obs_data_set_double(st, key, v);
			obs_source_update(f, st);
			obs_data_release(st);
		});
	}

	void writeBool(const char *key, bool v)
	{
		if (loading_)
			return;
		require([key, v](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;
			obs_data_set_bool(st, key, v);
			obs_source_update(f, st);
			obs_data_release(st);
		});
	}

	void writeString(const char *key, const char *v)
	{
		if (loading_)
			return;
		const std::string copy(v ? v : "");
		require([key, copy](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;
			obs_data_set_string(st, key, copy.c_str());
			obs_source_update(f, st);
			obs_data_release(st);
		});
	}

	/* Music on its own changes nothing: bands rise and fall, but they only
	 * move the image once a routing points a band at a parameter. If the user
	 * has never configured one, wire up a pair that is obviously visible -
	 * bass pumps the kaleidoscope zoom, treble shifts the hue. */
	void writeAudioDefaults()
	{
		if (loading_)
			return;
		require([](obs_source_t *f) {
			obs_data_t *st = obs_source_get_settings(f);
			if (!st)
				return;

			bool routed = false;
			for (int i = 1; i <= 4 && !routed; i++) {
				char key[32];
				snprintf(key, sizeof(key), "audio%d_on", i);
				routed = obs_data_get_bool(st, key);
			}
			if (routed) {
				obs_data_release(st);
				return;
			}

			/* Target indices are into sb_mod_targets[]: 21 = k_zoom,
			 * 12 = hue. Band 0 is bass, band 3 is treble. */
			/* Resolved by key, so reordering the parameter table does not
			 * silently repoint these defaults. Bands: 0 = bass, 3 = treble. */
			/* Master transform parameters, so the reaction is visible no matter
			 * which stages the current preset has enabled. */
			const int zoom_target = sb_target_index("mt_zoom");
			const int angle_target = sb_target_index("mt_angle");
			if (zoom_target < 0 || angle_target < 0) {
				obs_data_release(st);
				return;
			}

			obs_data_set_bool(st, "audio1_on", true);
			obs_data_set_int(st, "audio1_band", 0);
			obs_data_set_int(st, "audio1_target", zoom_target);
			obs_data_set_double(st, "audio1_depth", 0.50);

			obs_data_set_bool(st, "audio2_on", true);
			obs_data_set_int(st, "audio2_band", 3);
			obs_data_set_int(st, "audio2_target", angle_target);
			obs_data_set_double(st, "audio2_depth", 0.50);

			obs_source_update(f, st);
			obs_data_release(st);
			blog(LOG_INFO, "[spacebeam] dock: wired default audio routings");
		});
	}

	/* Label and colour the master switch so its state is obvious at a glance. */
	void stylePowerButton(bool on)
	{
		if (!powerBtn_)
			return;
		powerBtn_->setText(on ? tr("Effect: ON  —  click to bypass")
				      : tr("Effect: BYPASSED  —  click to enable"));
		powerBtn_->setStyleSheet(
			on ? QStringLiteral("QPushButton { background:#1b5e20; color:#ffffff;"
					    " font-weight:bold; border-radius:4px; }"
					    " QPushButton:hover { background:#2e7d32; }")
			   : QStringLiteral("QPushButton { background:#5d4037; color:#ffcc80;"
					    " font-weight:bold; border-radius:4px; }"
					    " QPushButton:hover { background:#6d4c41; }"));
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

			/* Showing it in the dock area can quietly succeed as a hidden tab,
			 * which looks like the panel vanished. Always float it: the user
			 * can dock it themselves and that is their call. */
			dock->setFloating(true);
			dock->resize(340, 640);
			const QRect frame = win->frameGeometry();
			dock->move(frame.right() - 380, frame.top() + 120);
			dock->show();
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
