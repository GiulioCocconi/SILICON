/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#pragma once

#include <functional>
#include <utility>

#include <QObject>
#include <QPointer>

namespace SILICON::ui {

/** Makes an asynchronous callback inert when its owner disappears. */
template <class Callback> auto guardCallback(const QObject* owner, Callback callback)
{
  return [owner = QPointer<const QObject>(owner), guarded = owner != nullptr,
          callback = std::move(callback)](auto&&... args) mutable {
    if (!guarded || owner)
      std::invoke(callback, std::forward<decltype(args)>(args)...);
  };
}

/**
 * A connection context that expires before its owner's state and Qt children.
 * Declare it after the state callbacks use. Destroying the context disconnects
 * subscriptions and discards queued calls; a connection handle alone does neither.
 */
class CallbackScope {
public:
  QObject* context() noexcept { return &receiver; }

  template <class Sender, class Signal, class Callback>
  QMetaObject::Connection connect(Sender* sender, Signal signal, Callback&& callback)
  {
    return QObject::connect(sender, signal, &receiver, std::forward<Callback>(callback));
  }

  template <class Callback> auto guard(Callback callback) const
  {
    return guardCallback(&receiver, std::move(callback));
  }

private:
  QObject receiver;
};

/** Removes a filter before either its owner or the watched widget is torn down. */
class ScopedEventFilter {
public:
  ScopedEventFilter()                                    = default;
  ScopedEventFilter(const ScopedEventFilter&)            = delete;
  ScopedEventFilter& operator=(const ScopedEventFilter&) = delete;
  ~ScopedEventFilter() { reset(); }

  void reset(QObject* target = nullptr, QObject* filter = nullptr)
  {
    if (watched && observer)
      watched->removeEventFilter(observer);
    watched  = target;
    observer = filter;
    if (watched && observer)
      watched->installEventFilter(observer);
  }

  [[nodiscard]] QObject* target() const noexcept { return watched; }

private:
  QPointer<QObject> watched;
  QPointer<QObject> observer;
};

}  // namespace SILICON::ui
