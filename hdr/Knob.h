#ifndef KNOB_H
#define KNOB_H

class Knob{
    public:
        Knob(std::bitset<2> _knobState, int _max = 8, int _min = 0);
        void updateValue(std::bitset<2> previousState, std::bitset<2> currentState);
        void setMax(int newMax);
        void setMin(int newMin);
        void getRotation();
}

#endif