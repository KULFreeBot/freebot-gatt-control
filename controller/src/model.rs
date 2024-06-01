//! Model of a FreeBot which is used as a digital twin

use std::fmt::Display;

#[derive(Clone, Copy, Debug, PartialEq)]
enum Led {
    On,
    Off,
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Voltage {
    v: i32,
}

#[derive(Clone, Copy, Debug)]
struct Motor {
    rpm: i32,
    angle: i32,
}

#[derive(Clone, Debug)]
pub struct FreeBot {
    name: String,
    d15: Led,
    d16: Led,
    m1: Motor,
    m2: Motor,
    m3: Motor,
    m4: Motor,
    v_cap: Voltage,
    v_motor: Voltage,
}

impl FreeBot {
    pub fn new(n: String) -> Self {
        FreeBot {
            name: n,
            d15: Led::Off,
            d16: Led::Off,
            m1: Motor { rpm: 0, angle: 0 },
            m2: Motor { rpm: 0, angle: 0 },
            m3: Motor { rpm: 0, angle: 0 },
            m4: Motor { rpm: 0, angle: 0 },
            v_cap: Voltage { v: 0 },
            v_motor: Voltage { v: 0 },
        }
    }

    pub fn name(&self) -> String {
        self.name.to_owned()
    }

    pub fn update_motor_angles(
        &mut self,
        front_left: i32,
        front_right: i32,
        back_left: i32,
        back_right: i32,
    ) {
        self.m1.angle = front_left;
        self.m2.angle = front_right;
        self.m3.angle = back_left;
        self.m4.angle = back_right;
    }

    pub fn update_motor_rpm(
        &mut self,
        front_left: i32,
        front_right: i32,
        back_left: i32,
        back_right: i32,
    ) {
        self.m1.rpm = front_left;
        self.m2.rpm = front_right;
        self.m3.rpm = back_left;
        self.m4.rpm = back_right;
    }

    pub fn update_cap_voltage(&mut self, v: i32) {
        self.v_cap.v = v;
    }

    pub fn update_motor_voltage(&mut self, v: i32) {
        self.v_motor.v = v;
    }

    pub fn update_leds(&mut self, d15: u8, d16: u8) {
        self.d15 = if d15 == 0 { Led::Off } else { Led::On };
        self.d16 = if d16 == 0 { Led::Off } else { Led::On };
    }
}

impl Display for FreeBot {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let name = format!("{:^23}", self.name);

        let v_c = format!("{:4}mV", self.v_cap.v);
        let v_m = format!("{:4}mV", self.v_motor.v);

        let d15 = match self.d15 {
            Led::On => " [X] ",
            Led::Off => " [ ] ",
        };

        let d16 = match self.d16 {
            Led::On => " [X] ",
            Led::Off => " [ ] ",
        };

        let a_m1 = format!(" {:4}º ", ((self.m1.angle % 360) + 360) % 360); // Calculate modulo instead of remainder
        let r_m1 = format!(" {:3}/min", self.m1.rpm);

        let a_m2 = format!(" {:4}º ", ((self.m2.angle % 360) + 360) % 360); // Calculate modulo instead of remainder
        let r_m2 = format!(" {:3}/min", self.m2.rpm);

        let a_m3 = format!(" {:4}º ", ((self.m3.angle % 360) + 360) % 360); // Calculate modulo instead of remainder
        let r_m3 = format!(" {:3}/min", self.m3.rpm);

        let a_m4 = format!(" {:4}º ", ((self.m4.angle % 360) + 360) % 360); // Calculate modulo instead of remainder
        let r_m4 = format!(" {:3}/min", self.m4.rpm);

        write!(
            f,
            r#"
            +-------------------------+            
+-----------+                         +-----------+
|           | {name                 } |           |
|           |                         |           |
| {a_m1 }   |  Vcap   = {v_c }        | {a_m2 }   |
| {r_m1  }  |  Vmotor = {v_m }        | {r_m2  }  |
|           |                         |           |
|           |                         |           |
+-----------+                         +-----------+
            |                         |            
+-----------+          {d16}          +-----------+
|           |          {d15}          |           |
|           |                         |           |
| {a_m3 }   |                         | {a_m4 }   |
| {r_m3  }  |                         | {r_m4  }  |
|           |                         |           |
|           |                         |           |
+-----------+                         +-----------+
            +-------------------------+            "#
        )
    }
}
