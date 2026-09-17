#pragma once

// Minimal 3D rotation math needed to compare the phone's current
// orientation against the one captured at calibration. Kept separate from
// MotionProcessor (which owns the motion *state machine*) so the math
// itself has a single reason to change.

struct Quaternion {
    float w = 1.f;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

// Row-major 3x3 rotation matrix.
struct Matrix3x3 {
    float m[3][3];

    // Builds the rotation matrix that carries a vector from device frame to
    // world frame, given the device's orientation quaternion. Assumes q is
    // normalized (true for Android's game rotation vector output).
    static Matrix3x3 FromQuaternion(const Quaternion& q) {
        const float w = q.w, x = q.x, y = q.y, z = q.z;
        Matrix3x3 r;
        r.m[0][0] = 1 - 2 * (y * y + z * z);
        r.m[0][1] = 2 * (x * y - z * w);
        r.m[0][2] = 2 * (x * z + y * w);
        r.m[1][0] = 2 * (x * y + z * w);
        r.m[1][1] = 1 - 2 * (x * x + z * z);
        r.m[1][2] = 2 * (y * z - x * w);
        r.m[2][0] = 2 * (x * z - y * w);
        r.m[2][1] = 2 * (y * z + x * w);
        r.m[2][2] = 1 - 2 * (x * x + y * y);
        return r;
    }
};

inline Matrix3x3 Transpose(const Matrix3x3& m) {
    Matrix3x3 t;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            t.m[row][col] = m.m[col][row];
        }
    }
    return t;
}

inline Matrix3x3 operator*(const Matrix3x3& a, const Matrix3x3& b) {
    Matrix3x3 r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r.m[row][col] = a.m[row][0] * b.m[0][col] +
                             a.m[row][1] * b.m[1][col] +
                             a.m[row][2] * b.m[2][col];
        }
    }
    return r;
}
