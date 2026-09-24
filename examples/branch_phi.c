volatile int left_marker;
volatile int right_marker;

int branch_phi(int condition, int left, int right) {
    int value;

    if (condition) {
        (void)left_marker;
        value = left + 1;
    } else {
        (void)right_marker;
        value = right + 2;
    }

    return value;
}
