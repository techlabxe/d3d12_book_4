#include "Camera.h"
#include <cmath>

using namespace DirectX;

Camera::Camera() : m_isDragged(false)
{
    m_mtxView = XMMatrixIdentity();
    m_mtxProj = XMMatrixIdentity();
    m_buttonType = -1;
}

void Camera::SetLookAt(XMFLOAT3 vPos, XMFLOAT3 vTarget, XMFLOAT3 vUp)
{
    m_eye = XMLoadFloat3(&vPos);
    m_target = XMLoadFloat3(&vTarget);
    m_up = XMLoadFloat3(&vUp);
    m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
}

void Camera::SetPerspective(float fovY, float aspect, float znear, float zfar)
{
    m_mtxProj = XMMatrixPerspectiveFovRH(fovY, aspect, znear, zfar);
}

void Camera::OnMouseButtonDown(int buttonType)
{
    m_buttonType = buttonType;
}

bool Camera::OnMouseMove(float dx, float dy)
{
    if (m_buttonType < 0)
    {
        return false;
    }
    if (m_buttonType == 0)
    {
        CalcOrbit(dx, dy);
        return true;
    }
    if (m_buttonType == 1)
    {
        CalcDolly(dy);
        return true;
    }
    if (m_buttonType == 2)
    {
        CalcPan(dx, dy);
        return true;
    }
    return false;
}
void Camera::OnMouseButtonUp()
{
    m_buttonType = -1;
}

void Camera::CalcOrbit(float dx, float dy)
{
    auto toEye = m_eye - m_target;
    auto toEyeLength = XMVectorGetX(XMVector3Length(toEye));
    toEye = XMVector3Normalize(toEye);

    auto phi = std::atan2(XMVectorGetX(toEye), XMVectorGetZ(toEye)); // 方位角.
    auto theta = std::acos(XMVectorGetY(toEye));  // 仰角.

    // ウィンドウのサイズ移動時には
    //  - 方位角は 360度分回る
    //  - 仰角は 約180度分回る.
    auto x = (XM_PI + phi) / XM_2PI;
    auto y = theta / XM_PI;

    x += dx;
    y -= dy;
    y = std::fmax(0.02f, std::fmin(y, 0.98f));

    // 割合からラジアン角へ変換.
    phi = x * XM_2PI;
    theta = y * XM_PI;

    auto st = std::sinf(theta);
    auto sp = std::sinf(phi);
    auto ct = std::cosf(theta);
    auto cp = std::cosf(phi);

    // 各成分より新カメラ位置への3次元ベクトルを生成.
    auto newToEye = XMVector3Normalize(XMVectorSet(-st * sp, ct, -st * cp, 0.0f));
    newToEye *= toEyeLength;
    m_eye = m_target + newToEye;

    m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
}

void Camera::CalcDolly(float d)
{
    auto toTarget = m_target - m_eye;
    auto toTargetLength = XMVectorGetX(XMVector3Length(toTarget));
    if (toTargetLength < FLT_EPSILON) {
        return;
    }
    toTarget = XMVector3Normalize(toTarget);

    auto delta = toTargetLength * d;
    m_eye += toTarget * delta;

    m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
}

void Camera::CalcPan(float dx, float dy)
{
    auto toTarget = m_target - m_eye;
    auto toTargetLength = XMVectorGetX(XMVector3Length(toTarget));

    m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
    XMMATRIX m = XMMatrixTranspose(m_mtxView);
    XMVECTOR right = m.r[0] * dx * toTargetLength;
    XMVECTOR top = m.r[1] * dy * toTargetLength;
    m_eye += right;
    m_target += right;
    m_eye += top;
    m_target += top;

    m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
}


