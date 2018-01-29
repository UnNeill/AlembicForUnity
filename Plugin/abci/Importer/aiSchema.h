#pragma once

class aiSampleBase;
class aiSchemaBase;


class aiSampleBase
{
public:
    aiSampleBase(aiSchemaBase *schema);
    virtual ~aiSampleBase();

    virtual aiSchemaBase* getSchema() const { return m_schema; }
    virtual void doInterpolation() {}

    const aiConfig& getConfig() const;

public:
    float m_current_time_offset = 0;
    float m_current_time_interval = 0;
protected:
    aiSchemaBase *m_schema = nullptr;
};


class aiSchemaBase
{
public:
    using aiPropertyPtr = std::unique_ptr<aiProperty>;

    aiSchemaBase(aiObject *obj);
    virtual ~aiSchemaBase();

    aiObject* getObject();
    const aiConfig& getConfig() const;

    virtual aiSampleBase* updateSample(const abcSampleSelector& ss) = 0;
    virtual aiSampleBase* getSample() = 0;

    bool isConstant() const;
    bool isDataUpdated() const;
    void markForceUpdate();
    int getNumProperties() const;
    aiProperty* getPropertyByIndex(int i);
    aiProperty* getPropertyByName(const std::string& name);

protected:
    virtual abcProperties getAbcProperties() = 0;
    void setupProperties();
    void updateProperties(const abcSampleSelector& ss);

protected:
    aiObject *m_obj = nullptr;
    bool m_constant = false;
    bool m_data_updated = false;
    bool m_force_update = false;
    std::vector<aiPropertyPtr> m_properties; // sorted vector
};


template <class Traits>
class aiTSchema : public aiSchemaBase
{
public:
    using Sample = typename Traits::SampleT;
    using SamplePtr = std::shared_ptr<Sample>;
    using SampleCont = std::map<int64_t, SamplePtr>;
    using AbcSchema = typename Traits::AbcSchemaT;
    using AbcSchemaObject = Abc::ISchemaObject<AbcSchema>;


    aiTSchema(aiObject *obj)
        : aiSchemaBase(obj)
    {
        AbcSchemaObject abcObj(obj->getAbcObject(), Abc::kWrapExisting);
        m_schema = abcObj.getSchema();
        m_constant = m_schema.isConstant();
        m_time_sampling = m_schema.getTimeSampling();
        m_num_samples = static_cast<int64_t>(m_schema.getNumSamples());
        setupProperties();
    }

    virtual ~aiTSchema()
    {}

    int getTimeSamplingIndex() const
    {
        return m_obj->getContext()->getTimeSamplingIndex(m_schema.getTimeSampling());
    }

    int getSampleIndex(const abcSampleSelector& ss) const
    {
        return static_cast<int>(ss.getIndex(m_time_sampling, m_num_samples));
    }

    float getSampleTime(const abcSampleSelector& ss) const
    {
        return static_cast<float>(m_time_sampling->getSampleTime(ss.getRequestedIndex()));
    }

    int getNumSamples() const
    {
        return static_cast<int>(m_num_samples);
    }

    aiSampleBase* updateSample(const abcSampleSelector& ss) override
    {
        Sample* sample = nullptr;
        int64_t sample_index = getSampleIndex(ss);
        auto& config = getConfig();

        if (!m_the_sample || (!m_constant && sample_index != m_last_sample_index)) {
            sample = readSample(sample_index);
            if (sample != m_the_sample.get())
                m_the_sample.reset(sample);
        }
        else {
            sample = m_the_sample.get();
            if (!m_force_update && (m_constant || !config.interpolate_samples))
            {
                sample = nullptr;
            }
        }

        m_force_update = false;
        m_last_sample_index = sample_index;

        if (sample) {
            if (config.interpolate_samples) {
                auto interval = m_schema.getTimeSampling()->getTimeSamplingType().getTimePerCycle();
                auto index_time = m_time_sampling->getSampleTime(sample_index);
                sample->m_current_time_offset = (float)std::max(0.0, std::min((ss.getRequestedTime() - index_time) / interval, 1.0));
                sample->m_current_time_interval = (float)interval;
                sample->doInterpolation();
            }
            m_data_updated = true;
        }
        else {
            m_data_updated = false;
        }
        updateProperties(ss);

        return sample;
    }

    Sample* getSample() override
    {
        return m_the_sample.get();
    }

protected:
    virtual Sample* readSample(const uint64_t idx) = 0;

    AbcGeom::ICompoundProperty getAbcProperties() override
    {
        return m_schema.getUserProperties();
    }

protected:
    AbcSchema m_schema;
    Abc::TimeSamplingPtr m_time_sampling;
    SamplePtr m_the_sample;
    int64_t m_num_samples = 0;
    int64_t m_last_sample_index = -1;
};
