
#ifndef TOUCHPANEL_H__
#define TOUCHPANEL_H__

/* Pre-defined definition */
#define TPD_TYPE_CAPACITIVE
#define TPD_TYPE_RESISTIVE
#define TPD_POWER_SOURCE         MT6573_POWER_VGP2
#define TPD_I2C_NUMBER           0
#define TPD_WAKEUP_TRIAL         60
#define TPD_WAKEUP_DELAY         100

#define TPD_DELAY                (2*HZ/100)
//#define TPD_RES_X                480
//#define TPD_RES_Y                800
#define TPD_CALIBRATION_MATRIX  {962,0,0,0,1600,0,0,0};

#define TPD_HAVE_BUTTON
#define TPD_BUTTON_HEIGHT	480
#define TPD_KEY_COUNT           4
#define TPD_KEYS                { KEY_MENU, KEY_SEARCH,KEY_BACK ,KEY_HOME}
#define TPD_KEYS_DIM            {{53,505,106,50},{159,505,106,50},{265,505,106,50},{371,505,106,50}}

//#define TPD_HAVE_CALIBRATION
//#define TPD_HAVE_BUTTON
#define TPD_HAVE_TREMBLE_ELIMINATION

#endif /* TOUCHPANEL_H__ */
