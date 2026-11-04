// Negative test: sending an undeclared selector to a typed receiver is banned in closed-world dispatch
@interface StrictSensor
- (int)readValue;
@end

void test_undeclared_selector(StrictSensor *sensor) {
    // 'calibrateSensor' is not declared on StrictSensor
    [sensor calibrateSensor];
}
