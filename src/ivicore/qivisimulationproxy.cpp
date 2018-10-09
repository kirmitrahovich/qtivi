/****************************************************************************
**
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtIvi module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: LGPL-3.0
**
****************************************************************************/

#include "qivisimulationproxy.h"
#include "qivisimulationengine.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QQmlInfo>

#include <private/qmetaobjectbuilder_p.h>

QT_BEGIN_NAMESPACE
Q_LOGGING_CATEGORY(qLcIviSimulationEngine, "qt.ivi.simulationengine");

namespace qtivi_private {

QIviSimulationProxyBase::QIviSimulationProxyBase(QMetaObject *staticMetaObject, QObject *instance, const QHash<int, int> &methodMap, QObject *parent)
    : QObject(parent)
    , m_noSimulationEngine(false)
    , m_instance(instance)
    , m_staticMetaObject(staticMetaObject)
    , m_methodMap(methodMap)
{
}

const QMetaObject *QIviSimulationProxyBase::metaObject() const
{
    // Copied from moc_ class code
    // A dynamicMetaObject is created when the type is used from QML and new functions/properties
    // are added. This makes sure that we can access these from C++ as well
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : m_staticMetaObject;
}

void *QIviSimulationProxyBase::qt_metacast(const char *classname)
{
    if (!classname)
        return nullptr;
    return m_instance->qt_metacast(classname);
}

int QIviSimulationProxyBase::qt_metacall(QMetaObject::Call call, int methodId, void **a)
{
    if (m_noSimulationEngine)
        return -1;

    if (call == QMetaObject::InvokeMetaMethod) {
        // When a forwarded signal from the registered instance gets in. Directly call the signal here as well
        if (sender() == m_instance) {
            // The static MetaObject uses local ids, so we need to subtract the offset
            QMetaObject::activate(this, m_staticMetaObject, methodId - m_staticMetaObject->methodOffset(), a);
            return 0;
        }
        return m_instance->qt_metacall(call, m_methodMap.key(methodId), a);
    }
    return m_instance->qt_metacall(call, methodId, a);
}

void QIviSimulationProxyBase::classBegin()
{
}

void QIviSimulationProxyBase::componentComplete()
{
    setProperty("Base", QVariant::fromValue(m_instance));
}

QMetaObject QIviSimulationProxyBase::buildObject(const QMetaObject *metaObject, QHash<int, int> &methodMap, QIviSimulationProxyBase::StaticMetacallFunction metaCallFunction)
{
    QMetaObjectBuilder builder;
    const QString name = QString(QStringLiteral("QIviSimulationProxy_%1")).arg(QLatin1String(metaObject->className()));
    builder.setClassName(qPrintable(name));
    builder.setSuperClass(&QObject::staticMetaObject);
    builder.setStaticMetacallFunction(metaCallFunction);

    // Build the MetaObject ourself
    // This is needed as QML uses the static_metacall for reading the properties and every QMetaObject
    // has its own set. But as we need to intercept this to forward it to the registered instance, we
    // build our MetaObject completely and have one static_metacall function for all properties
    const QMetaObject *mo = metaObject;

    //Search for the QObject base class to know which offset we need to start building from
    const QMetaObject *superClass = mo->superClass();
    while (qstrcmp(superClass->className(), "QObject") != 0) {
        superClass = superClass->superClass();
    }
    const int methodOffset = superClass->methodCount();
    const int propertyOffset = superClass->propertyCount();

    //Fill the mapping for all QObject methods.
    for (int i=0; i<methodOffset; ++i)
        methodMap.insert(i, i);

    //Add all signals
    qCDebug(qLcIviSimulationEngine) << "Signal Mapping: Original -> Proxy";
    for (int index = methodOffset; index < mo->methodCount(); ++index) {
        QMetaMethod mm = mo->method(index);
        if (mm.methodType() == QMetaMethod::Signal) {
            auto mb = builder.addMethod(mm);
            qCDebug(qLcIviSimulationEngine) << index << "->" << methodOffset + mb.index();
            methodMap.insert(index, methodOffset + mb.index());
        }
    }

    //Add all other methods
    qCDebug(qLcIviSimulationEngine) << "Method Mapping: Original -> Proxy";
    for (int index = methodOffset; index < mo->methodCount(); ++index) {
        QMetaMethod mm = mo->method(index);
        if (mm.methodType() != QMetaMethod::Signal) {
            auto mb = builder.addMethod(mm);
            qCDebug(qLcIviSimulationEngine) << index << "->" << methodOffset + mb.index();
            methodMap.insert(index, methodOffset + mb.index());
        }
    }

    //Add all properties
    for (int index = propertyOffset; index < mo->propertyCount(); ++index) {
        QMetaProperty prop = mo->property(index);
        builder.addProperty(prop);
    }
    //Add a Base property which works like a attached property
    builder.addProperty("Base", "QObject *");

    //Debugging output
    if (qLcIviSimulationEngine().isDebugEnabled()) {
        qCDebug(qLcIviSimulationEngine) << "Original Object:";
        for (int i=0; i < mo->methodCount(); i++) {
            QMetaMethod method = mo->method(i);
            qCDebug(qLcIviSimulationEngine) << "method: " << method.methodIndex() << method.methodSignature();
        }
        for (int i=0; i < mo->propertyCount(); i++) {
            QMetaProperty prop = mo->property(i);
            qCDebug(qLcIviSimulationEngine) << "property:" << prop.propertyIndex() << prop.name();
            QMetaMethod method = prop.notifySignal();
            qCDebug(qLcIviSimulationEngine) << "signal: " << method.methodIndex() << method.methodSignature();
        }

        qCDebug(qLcIviSimulationEngine) << "Proxy Object:";
        mo = builder.toMetaObject();
        for (int i=0; i < mo->methodCount(); i++) {
            QMetaMethod method = mo->method(i);
            qCDebug(qLcIviSimulationEngine) << "method: " << method.methodIndex() << method.methodSignature();
        }
        for (int i=0; i < mo->propertyCount(); i++) {
            QMetaProperty prop = mo->property(i);
            qCDebug(qLcIviSimulationEngine) << "property:" << prop.propertyIndex() << prop.name();
            QMetaMethod method = prop.notifySignal();
            qCDebug(qLcIviSimulationEngine) << "signal: " << method.methodIndex() << method.methodSignature();
        }
    }

    return *builder.toMetaObject();
}

bool QIviSimulationProxyBase::callQmlMethod(const char *function, QGenericReturnArgument ret, QGenericArgument val0, QGenericArgument val1, QGenericArgument val2, QGenericArgument val3, QGenericArgument val4, QGenericArgument val5, QGenericArgument val6, QGenericArgument val7, QGenericArgument val8, QGenericArgument val9)
{
    if (m_noSimulationEngine)
        return false;

    //Prevent recursion
    static bool recursionGuard = false;
    if (recursionGuard)
        return false;

    recursionGuard = true;

    bool functionExecuted = false;
    const QMetaObject *mo = metaObject();

    // Only invoke the functions declared in QML.
    // Once a function/property is added to a type a new MetaObject gets created which contains
    // _QML_ in the name.
    if (QString::fromLatin1(mo->className()).contains(QLatin1String("_QML_"))) {
        for (int i=mo->methodOffset(); i<mo->methodCount(); i++) {
            //qDebug() << "CHECKING FOR: " << function << mo->method(i).name();
            if (mo->method(i).name() != function)
                continue;
            //qDebug() << "EXECUTING";
            functionExecuted = QMetaObject::invokeMethod(this, function, ret, val0, val1, val2, val3, val4, val5, val6, val7, val8, val9);
            break;
        }
    }
    recursionGuard = false;
    return functionExecuted;
}

void QIviSimulationProxyBase::setup(QIviSimulationEngine *engine)
{
    if (engine != qmlEngine(this)) {
        qmlWarning(this) << "QIviSimulationProxy can only be used in the same Engine it is registered in";
        m_noSimulationEngine = true;
        return;
    }

    // Connect all signals from the instance to the signals of this metaobject.
    // This is needed to relay the signals from the instance to this instance and to QML
    const QMetaObject *mo = m_instance->metaObject();
    for (int i=0; i<mo->methodCount(); i++) {
        QMetaMethod mm = mo->method(i);
        if (mm.methodType() != QMetaMethod::Signal)
            continue;
        connect(m_instance, mm, this, m_staticMetaObject->method(m_methodMap.value(i)));
    }
}

} //namespace

QT_END_NAMESPACE
