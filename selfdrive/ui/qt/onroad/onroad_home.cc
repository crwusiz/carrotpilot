#include "selfdrive/ui/qt/onroad/onroad_home.h"

#include <QPainter>
#include <QStackedLayout>


#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QDialog>


#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/carrot.h"
#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map_helpers.h"
#include "selfdrive/ui/qt/maps/map_panel.h"
#endif

class OverlayDialog : public QWidget {
  Q_OBJECT

public:
  explicit OverlayDialog(QWidget* parent = nullptr) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint); // 다이얼로그처럼 동작
    setStyleSheet("background-color: rgba(0, 0, 0, 0.8); border-radius: 10px;");
    resize(400, 300); // 기본 크기 설정
  }

  void setContent(QWidget* content) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(content);
    layout->setMargin(0);
    setLayout(layout);
  }
};

OnroadWindow::OnroadWindow(QWidget *parent) : QOpenGLWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  //main_layout->setContentsMargins(UI_BORDER_SIZE, 0, UI_BORDER_SIZE, 0);

  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_ROAD, this);
    split->insertWidget(0, arCam);
  }

  if (getenv("MAP_RENDER_VIEW")) {
    CameraWidget *map_render = new CameraWidget("navd", VISION_STREAM_MAP, this);
    split->insertWidget(0, map_render);
  }

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  // setup stacking order
  alerts->raise();

  setAttribute(Qt::WA_OpaquePaintEvent);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);
}

void OnroadWindow::updateState(const UIState &s) {
  UIState* ss = uiState();
  if (!s.scene.started) {
    ss->scene._current_carrot_display_prev = -1;
    return;
  }

  //alerts->updateState(s);
  ui_update_alert(OnroadAlerts::getAlert(*(s.sm), s.scene.started_frame));
  if (s.scene.map_on_left) {
    split->setDirection(QBoxLayout::LeftToRight);
  } else {
    split->setDirection(QBoxLayout::RightToLeft);
  }
  nvg->updateState(s);

  QColor bgColor = bg_colors[s.status];
  QColor bgColor_long = bg_colors[s.status];
  const SubMaster& sm = *(s.sm);
  const auto car_control = sm["carControl"].getCarControl();
  auto selfdrive_state = sm["selfdriveState"].getSelfdriveState();

  //if (s.status == STATUS_DISENGAGED && car_control.getLatActive()) {
  //    bgColor = bg_colors[STATUS_LAT_ACTIVE];
  //}
  const auto car_state = sm["carState"].getCarState();
  if (car_state.getSteeringPressed()) {
      bgColor = bg_colors[STATUS_OVERRIDE];
  }
  else if (car_control.getLatActive()) {
      bgColor = bg_colors[STATUS_ENGAGED];
  }
  else if (car_state.getLatEnabled()) {
      bgColor = bg_colors[STATUS_ACTIVE];
  }
  else
      bgColor = bg_colors[STATUS_DISENGAGED];

  if (car_state.getGasPressed()) {
      bgColor_long = bg_colors[STATUS_OVERRIDE];
  }
  else if (selfdrive_state.getEnabled()) {
      bgColor_long = bg_colors[STATUS_ENGAGED];
  }
  else if (car_state.getCruiseState().getAvailable()) {
	  bgColor_long = bg_colors[STATUS_ACTIVE];
  }
  else
      bgColor_long = bg_colors[STATUS_DISENGAGED];
  if (bg != bgColor || bg_long != bgColor_long) {
    // repaint border
    bg = bgColor;
    bg_long = bgColor_long;
    //update();
  }
  update();
  if (true) { //carrot_display > 0) {
      int carrot_display = 0;
      Params	params_memory{ "/dev/shm/params" };
      QString command = QString::fromStdString(params_memory.get("CarrotManCommand"));
      if (command.startsWith("DISPLAY ")) {
        QString display_cmd = command.mid(8);
        if (display_cmd == "TOGGLE") {
          carrot_display = 5;
          printf("Display toggle\n");
        }
        else if (display_cmd == "DEFAULT") {
          carrot_display = 1;
          printf("Display 1\n");
        }
        else if (display_cmd == "ROAD") {
          carrot_display = 2;
          printf("Display 2\n");
        }
        else if (display_cmd == "MAP") {
          carrot_display = 3;
          printf("Display 3\n");
        }
        else if (display_cmd == "FULLMAP") {
          carrot_display = 4;
          printf("Display 4\n");
        }
        params_memory.putNonBlocking("CarrotManCommand", "");
      }


      if (carrot_display == 5) ss->scene._current_carrot_display = (ss->scene._current_carrot_display % 3) + 1;
      else if(carrot_display > 0) ss->scene._current_carrot_display = carrot_display;
      if (map == nullptr && ss->scene._current_carrot_display > 2) ss->scene._current_carrot_display = 1;
      //printf("_current_carrot_display2=%d\n", _current_carrot_display);
      //if (offroad) _current_carrot_display = 1;
      switch (ss->scene._current_carrot_display) {
      case 1: // default
          if (map != nullptr) map->setVisible(false);
          if (ss->scene._current_carrot_display_prev != ss->scene._current_carrot_display) ss->scene._display_time_count = 100; // 100: about 5 seconds
          if (ss->scene._display_time_count-- <= 0) ss->scene._current_carrot_display = 2; // change to road view
          break;
      case 2: // road          
          if (map != nullptr) map->setVisible(false);
          break;
      case 3: // map
          if (map == nullptr) ss->scene._current_carrot_display = 1;
          else {
              map->setVisible(true);
              //map->setFixedWidth(topWidget(this)->width() / 2 - UI_BORDER_SIZE);
          }
          break;
      case 4: // fullmap
          if (map == nullptr) ss->scene._current_carrot_display = 1;
          else {
              map->setVisible(true);
              //map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
              //map->setFixedWidth(topWidget(this)->width() - UI_BORDER_SIZE);
          }
          break;
      }
      ss->scene._current_carrot_display_prev = ss->scene._current_carrot_display;
  }
  
}

void OnroadWindow::mousePressEvent(QMouseEvent* e) {
  //printf("uiState()->scene.navigate_on_openpilot = %d\n", uiState()->scene.navigate_on_openpilot);
//#ifdef ENABLE_MAPS
//  if (map != nullptr) {
    // Switch between map and sidebar when using navigate on openpilot
    //bool sidebarVisible = geometry().x() > 0;
    //bool show_map = uiState()->scene.navigate_on_openpilot ? sidebarVisible : !sidebarVisible;
    //map->setVisible(show_map && !map->isVisible());
//  }
//#endif
  // propagation event to parent(HomeWindow)
  UIState* s = uiState();
  s->scene._current_carrot_display = (s->scene._current_carrot_display % 3) + 1;  // 4번: full map은 안보여줌.
  printf("_current_carrot_display1=%d\n", s->scene._current_carrot_display);
  QWidget::mousePressEvent(e);
}
//OverlayDialog* mapDialog = nullptr;
void OnroadWindow::offroadTransition(bool offroad) {
#ifdef ENABLE_MAPS
  if (!offroad) {
    if (map == nullptr && (!MAPBOX_TOKEN.isEmpty())) {
      printf("####### Initialize MapPanel\n");
#if 0
      auto m = new MapPanel(get_mapbox_settings());
      map = m;

      m->setFixedWidth(topWidget(this)->width() / 2 - UI_BORDER_SIZE);
      split->insertWidget(0, m);

      // hidden by default, made visible when navRoute is published
      m->setVisible(false);
#else
      OverlayDialog* mapDialog = new OverlayDialog(this);
      mapDialog->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
      mapDialog->setAttribute(Qt::WA_TranslucentBackground);
      mapDialog->setAttribute(Qt::WA_NoSystemBackground);

      // MapPanel 추가
      auto m = new MapPanel(get_mapbox_settings(), mapDialog);
      map = m;
      mapDialog->setContent(m);

      // 특정 위치에 배치 (오른쪽 하단)
      mapDialog->setGeometry(topWidget(this)->width() - 790 - UI_BORDER_SIZE, UI_BORDER_SIZE + 15, 775, topWidget(this)->height() - 400);

      mapDialog->hide(); // 기본적으로 숨김 상태
      mapDialog->show();
      mapDialog->raise();
      uiState()->scene._current_carrot_display = 3;

#endif
    }
  }
#endif
  alerts->clear();
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.beginNativePainting();
    UIState* s = uiState();
    extern void ui_draw_border(UIState * s, int w, int h, QColor bg, QColor bg_long);
    ui_draw_border(s, width(), height(), bg, bg_long);
    p.endNativePainting();
}


// OnroadWindow.cpp에서 OpenGL 초기화 및 그리기 구현
void OnroadWindow::initializeGL() {
    initializeOpenGLFunctions(); // QOpenGLFunctions 초기화

    // Parent widget을 위한 NanoVG 컨텍스트 생성
    //s->vg = nvgCreate(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
    //if (s->vg == nullptr) {
    //    printf("Could not init nanovg.\n");
    //    return;
    //}
}


#include "selfdrive/ui/qt/onroad/onroad_home.moc"
