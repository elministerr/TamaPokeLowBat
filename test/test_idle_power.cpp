#include "framework.h"
#include "shim/Arduino.h"
#include "../idle_power.h"
#include "../pet.h"

static void makeIdlePet(Pet &pet, IdlePower &power) {
  mockNvsReset();
  mockSetMillis(0);
  pet.begin();
  pet.chooseStarter(4);
  pet.eggTap(); pet.eggTap(); pet.eggTap();
  power.recordActivity();
}

TEST(idle_power, sleeps_at_90_seconds_and_shuts_down_at_120_total) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockAdvanceMillis(89999);
  CHECK(!power.update(pet));
  CHECK(!power.dimmed());
  CHECK(!pet.sleeping);
  mockAdvanceMillis(1);
  CHECK(!power.update(pet));
  CHECK(power.dimmed());
  CHECK(pet.sleeping);
  mockAdvanceMillis(29999);
  CHECK(!power.update(pet));
  CHECK(pet.sleeping);  // actualizar repetidamente no alterna dormir/despertar
  mockAdvanceMillis(1);
  CHECK(power.update(pet));
  CHECK(pet.sleeping);
}

TEST(idle_power, activity_restarts_both_deadlines_but_only_light_wakes_pet) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockAdvanceMillis(119999);
  CHECK(!power.update(pet));
  power.recordActivity();
  CHECK(!power.update(pet));
  CHECK(!power.dimmed());
  CHECK(pet.sleeping);
  mockAdvanceMillis(1);  // el plazo original de 120 s ya no debe apagar
  CHECK(!power.update(pet));
  pet.toggleLight();
  CHECK(!pet.sleeping);
  mockAdvanceMillis(89999);
  CHECK(!power.update(pet));
  CHECK(pet.sleeping);
  mockAdvanceMillis(30000);
  CHECK(power.update(pet));
}

TEST(idle_power, uses_fresh_clock_after_touch_in_same_loop) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockSetMillis(119999);  // comienzo del loop
  mockAdvanceMillis(2);  // la lectura tactil termina mas tarde
  power.recordActivity();
  CHECK(!power.update(pet));
  CHECK(!power.dimmed());
  CHECK(!pet.sleeping);
}

TEST(idle_power, deadlines_work_across_millis_wraparound) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockSetMillis(UINT32_MAX - 50000);
  power.recordActivity();
  mockAdvanceMillis(89999);
  CHECK(!power.update(pet));
  CHECK(!power.dimmed());
  mockAdvanceMillis(1);
  CHECK(!power.update(pet));
  CHECK(pet.sleeping);
  mockAdvanceMillis(30000);
  CHECK(power.update(pet));
}

TEST(idle_power, animations_do_not_extend_user_inactivity) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockAdvanceMillis(89999);
  pet.caress();  // un evento visual sin nueva entrada no reinicia el plazo
  CHECK(pet.showHeart());
  mockAdvanceMillis(1);
  CHECK(!power.update(pet));
  CHECK(power.dimmed());
  CHECK(pet.sleeping);
}

TEST(idle_power, manually_sleeping_pet_stays_asleep) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  pet.toggleLight();
  mockAdvanceMillis(90000);
  CHECK(!power.update(pet));
  CHECK(pet.sleeping);
  power.recordActivity();
  CHECK(!power.update(pet));
  CHECK(pet.sleeping);
}

TEST(idle_power, unhatched_egg_still_powers_off) {
  mockNvsReset();
  mockSetMillis(0);
  Pet pet;
  pet.begin();
  IdlePower power;
  power.recordActivity();
  mockAdvanceMillis(90000);
  CHECK(!power.update(pet));
  CHECK(power.dimmed());
  CHECK(pet.awaitingStarter());
  mockAdvanceMillis(30000);
  CHECK(power.update(pet));
}

TEST(idle_power, shutdown_saves_latest_state_and_rtc_for_sleeping_offline_progress) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  mockAdvanceMillis(120000);
  CHECK(power.update(pet));
  pet.energy = 20;
  pet.fullness = 70;
  CHECK(!pet.savePending());  // debe guardar aunque el save periodico no toque
  const uint32_t epoch = 1800000000;
  pet.saveForPowerOff(epoch);
  Pet rebooted;
  rebooted.begin();
  CHECK(rebooted.sleeping);
  CHECK_EQ(rebooted.energy, (uint8_t)20);
  CHECK_EQ(rebooted.fullness, (uint8_t)70);
  CHECK_EQ(rebooted.savedEpoch(), epoch);
  rebooted.syncClock(epoch + 10 * 60);
  CHECK(rebooted.sleeping);
  CHECK_EQ(rebooted.energy, (uint8_t)80);
  power.recordActivity();  // arrancar/tocar tampoco despierta a la mascota
  CHECK(!power.update(rebooted));
  CHECK(rebooted.sleeping);
  rebooted.toggleLight();
  CHECK(!rebooted.sleeping);
}

TEST(idle_power, shutdown_saves_even_if_rtc_read_fails) {
  Pet pet;
  IdlePower power;
  makeIdlePet(pet, power);
  const uint32_t epoch = 1800000000;
  pet.setClock(epoch);
  pet.energy = 17;
  pet.saveForPowerOff(0);
  Pet rebooted;
  rebooted.begin();
  CHECK_EQ(rebooted.energy, (uint8_t)17);
  CHECK_EQ(rebooted.savedEpoch(), epoch);
}
